/*
 * mod-self-found
 *
 * Foundation module for the WoW-Forever "Self-Found" ruleset (Kanboard #788): a
 * per-character, opt-in flag that cuts a character off from every path by which
 * gear or gold can enter from another player:
 *
 *   1. Trade      - blocked outright (OnPlayerCanInitTrade / OnPlayerCanSetTradeItem).
 *   2. Mail       - outgoing mail is blocked (OnPlayerCanSendMail). See the
 *                    README for why incoming mail is NOT blocked here.
 *   3. Auction House - the AH window itself refuses to open (MiscScript::
 *                    CanSendAuctionHello), with OnPlayerCanPlaceAuctionBid kept
 *                    as defense-in-depth against a bid/buyout sent without the
 *                    window (e.g. a modified client).
 *
 * The flag is stored in a small programmatic table in the characters DB, keyed
 * by the character's low guid, and cached in memory (loaded once at startup,
 * updated on every `.selffound on|off`) so none of the hooks above ever need a
 * DB round trip. This storage/cache shape is deliberately generic so a later
 * Hardcore ruleset (Kanboard #789) can reuse it (its own table, same pattern).
 *
 * Released under GNU GPL v2; redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"

// Playerbots fork header, used only to recognise a bot-controlled session so we
// don't chat-spam a bot with the login reminder. Enforcement itself is guid
// based and applies the same whether the character is human- or bot-controlled.
#include "Playerbots.h"

#include "SelfFound.h"

#include <unordered_set>

namespace SelfFound
{
    Config& GetConfig()
    {
        static Config cfg;
        return cfg;
    }
}

using namespace Acore::ChatCommands;

namespace
{
    // In-memory cache of every currently-flagged character's low guid. Only
    // ever touched from the world update thread (chat commands and script
    // hooks both run there on this core), so no locking is needed - the same
    // assumption mod-guild-tax's in-memory state relies on.
    std::unordered_set<uint32>& FlaggedGuids()
    {
        static std::unordered_set<uint32> guids;
        return guids;
    }

    void NotifyBlocked(Player* player, char const* what)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession())
            .PSendSysMessage("|cffff0000[Self-Found]|r This character cannot use {} while Self-Found is enabled.",
                what);
    }
}

namespace SelfFound
{
    void EnsureSchema()
    {
        // Deliberately NOT an SQL update file: on this fork a failing module SQL
        // aborts the whole worldserver boot, so we create the table
        // programmatically and tolerate failure at runtime instead (matches
        // mod-guild-tax's approach).
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `self_found_flags` ("
            "`guid` INT UNSIGNED NOT NULL, "
            "`enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1, "
            "PRIMARY KEY (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
    }

    void LoadFlags()
    {
        std::unordered_set<uint32>& guids = FlaggedGuids();
        guids.clear();

        if (QueryResult result = CharacterDatabase.Query("SELECT `guid` FROM `self_found_flags` WHERE `enabled` = 1"))
        {
            do
            {
                Field* fields = result->Fetch();
                guids.insert(fields[0].Get<uint32>());
            } while (result->NextRow());
        }
    }

    bool IsSelfFoundGuid(uint32 guidLow)
    {
        std::unordered_set<uint32> const& guids = FlaggedGuids();
        return guids.find(guidLow) != guids.end();
    }

    bool IsSelfFound(Player* player)
    {
        if (!player)
            return false;

        return IsSelfFoundGuid(player->GetGUID().GetCounter());
    }

    void SetSelfFound(Player* player, bool enabled)
    {
        if (!player)
            return;

        uint32 guidLow = player->GetGUID().GetCounter();

        CharacterDatabase.Execute(
            "INSERT INTO `self_found_flags` (`guid`, `enabled`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `enabled` = {}",
            guidLow, uint32(enabled ? 1 : 0), uint32(enabled ? 1 : 0));

        if (enabled)
            FlaggedGuids().insert(guidLow);
        else
            FlaggedGuids().erase(guidLow);
    }
}

// =====================================================================
//  CommandScript: `.selffound on|off|status`
// =====================================================================
class selffound_commandscript : public CommandScript
{
public:
    selffound_commandscript() : CommandScript("selffound_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable selfFoundTable =
        {
            { "on",     HandleSelfFoundOnCommand,     SEC_PLAYER, Console::No },
            { "off",    HandleSelfFoundOffCommand,    SEC_PLAYER, Console::No },
            { "status", HandleSelfFoundStatusCommand, SEC_PLAYER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "selffound", selfFoundTable },
        };

        return commandTable;
    }

    // v1 scope note: this is a self-service toggle, not a creation-time
    // commitment. There is no character-creation NPC / one-shot lock yet (out
    // of scope for #788, see README "Gaps"), so a player can flip the flag
    // back off at will. Good enough for the foundation; a Hardcore-style
    // ruleset built on top of this table may want a one-way lock.
    static bool HandleSelfFoundOnCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!SelfFound::GetConfig().Enable)
        {
            handler->SendSysMessage("Self-Found is currently disabled on this server.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (SelfFound::IsSelfFound(player))
        {
            handler->SendSysMessage("This character is already flagged Self-Found.");
            return true;
        }

        SelfFound::SetSelfFound(player, true);
        handler->PSendSysMessage(
            "Self-Found ENABLED for {}. Trade, the Auction House and outgoing mail are now blocked for this character.",
            player->GetName());
        return true;
    }

    static bool HandleSelfFoundOffCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!SelfFound::IsSelfFound(player))
        {
            handler->SendSysMessage("This character is not flagged Self-Found.");
            return true;
        }

        SelfFound::SetSelfFound(player, false);
        handler->SendSysMessage("Self-Found DISABLED for this character.");
        return true;
    }

    static bool HandleSelfFoundStatusCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (SelfFound::IsSelfFound(player))
            handler->SendSysMessage("Self-Found: ENABLED (trade, Auction House and outgoing mail are blocked).");
        else
            handler->SendSysMessage("Self-Found: disabled.");
        return true;
    }
};

// =====================================================================
//  PlayerScript: login reminder + trade/mail/auction-bid enforcement.
// =====================================================================
class SelfFoundPlayerScript : public PlayerScript
{
public:
    SelfFoundPlayerScript() : PlayerScript("SelfFound_PlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.AnnounceOnLogin)
            return;

        if (!player || !SelfFound::IsSelfFound(player))
            return;

        // Bots (including Michael's own altbots when not the active character)
        // don't read chat - skip the reminder for a bot-controlled session.
        if (GET_PLAYERBOT_AI(player))
            return;

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cffff0000[Self-Found]|r This character cannot trade with other players, "
            "use the Auction House, or send mail. Use '.selffound off' to opt out.");
    }

    [[nodiscard]] bool OnPlayerCanInitTrade(Player* player, Player* target) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.BlockTrade)
            return true;

        if (!SelfFound::IsSelfFound(player) && !SelfFound::IsSelfFound(target))
            return true;

        // Whichever side is flagged, tell the initiator why the window won't open.
        NotifyBlocked(player, "trade");
        return false;
    }

    [[nodiscard]] bool OnPlayerCanSetTradeItem(Player* player, Item* /*tradedItem*/, uint8 /*tradeSlot*/) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.BlockTrade)
            return true;

        // Defense in depth: OnPlayerCanInitTrade already keeps the trade window
        // from opening at all for a flagged character. This additionally covers
        // a trade window that was already open when the flag got toggled on.
        Player* other = player->GetTrader();
        if (!SelfFound::IsSelfFound(player) && !(other && SelfFound::IsSelfFound(other)))
            return true;

        return false;
    }

    [[nodiscard]] bool OnPlayerCanSendMail(Player* player, ObjectGuid /*receiverGuid*/, ObjectGuid /*mailbox*/,
        std::string& /*subject*/, std::string& /*body*/, uint32 /*money*/, uint32 /*COD*/, Item* /*item*/) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.BlockMail)
            return true;

        if (!SelfFound::IsSelfFound(player))
            return true;

        NotifyBlocked(player, "the mailbox to send mail");
        return false;
    }

    [[nodiscard]] bool OnPlayerCanPlaceAuctionBid(Player* player, AuctionEntry* /*auction*/) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.BlockAuctionHouse)
            return true;

        if (!SelfFound::IsSelfFound(player))
            return true;

        NotifyBlocked(player, "the Auction House");
        return false;
    }
};

// =====================================================================
//  MiscScript: refuse to even open the Auction House window.
// =====================================================================
class SelfFoundMiscScript : public MiscScript
{
public:
    SelfFoundMiscScript() : MiscScript("SelfFound_MiscScript") { }

    [[nodiscard]] bool CanSendAuctionHello(
        WorldSession const* session, ObjectGuid /*guid*/, Creature* /*creature*/) override
    {
        SelfFound::Config const& cfg = SelfFound::GetConfig();
        if (!cfg.Enable || !cfg.BlockAuctionHouse)
            return true;

        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player || !SelfFound::IsSelfFound(player))
            return true;

        NotifyBlocked(player, "the Auction House");
        return false;
    }
};

// =====================================================================
//  WorldScript: config load + schema/cache bootstrap.
// =====================================================================
class SelfFoundWorldScript : public WorldScript
{
public:
    SelfFoundWorldScript() : WorldScript("SelfFound_WorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        SelfFound::Config& cfg = SelfFound::GetConfig();
        cfg.Enable            = sConfigMgr->GetOption<bool>("SelfFound.Enable", true);
        cfg.BlockTrade        = sConfigMgr->GetOption<bool>("SelfFound.BlockTrade", true);
        cfg.BlockMail         = sConfigMgr->GetOption<bool>("SelfFound.BlockMail", true);
        cfg.BlockAuctionHouse = sConfigMgr->GetOption<bool>("SelfFound.BlockAuctionHouse", true);
        cfg.AnnounceOnLogin   = sConfigMgr->GetOption<bool>("SelfFound.AnnounceOnLogin", true);
    }

    void OnStartup() override
    {
        SelfFound::EnsureSchema();
        SelfFound::LoadFlags();
    }
};

// =====================================================================
//  Registration
// =====================================================================
void AddSelfFoundScripts()
{
    new selffound_commandscript();
    new SelfFoundPlayerScript();
    new SelfFoundMiscScript();
    new SelfFoundWorldScript();
}
