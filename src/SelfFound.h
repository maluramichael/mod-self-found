/*
 * mod-self-found - shared declarations.
 *
 * Foundation flag for the WoW-Forever "Self-Found" ruleset (Kanboard #788). A
 * character flagged Self-Found cannot trade with other players, cannot use the
 * Auction House, and cannot send mail. The flag is stored per character, keyed
 * by the character's low guid, so it can be reused as-is by a later Hardcore
 * ruleset (Kanboard #789).
 *
 * Released under GNU GPL v2; redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef MOD_SELF_FOUND_H
#define MOD_SELF_FOUND_H

class Player;

namespace SelfFound
{
    // Cached config (populated in WorldScript::OnAfterConfigLoad).
    struct Config
    {
        bool Enable            = true; // module master switch
        bool BlockTrade        = true; // block player <-> player trade
        bool BlockMail         = true; // block outgoing mail
        bool BlockAuctionHouse = true; // block the Auction House entirely
        bool AnnounceOnLogin   = true; // remind the player on login they're flagged
    };

    Config& GetConfig();

    // Creates the programmatic self_found_flags table in the characters DB if it
    // does not already exist. Safe to call repeatedly.
    void EnsureSchema();

    // (Re)loads the in-memory flagged-guid cache from the characters DB. Call
    // once at startup, after EnsureSchema().
    void LoadFlags();

    // True if the given character (by low guid) is currently flagged Self-Found.
    // Checks only the in-memory cache - no DB hit.
    bool IsSelfFoundGuid(uint32 guidLow);

    // Convenience overload for an online Player*. Returns false for a null player.
    bool IsSelfFound(Player* player);

    // Sets/clears the flag for a character: persists it to the characters DB and
    // updates the in-memory cache. No-op for a null player.
    void SetSelfFound(Player* player, bool enabled);
}

#endif // MOD_SELF_FOUND_H
