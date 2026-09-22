# mod-self-found

An [AzerothCore](https://www.azerothcore.org/) module (WotLK 3.3.5a) that adds an opt-in
**Self-Found** ruleset: a flagged character must find its own gear — no trading, no auction
house, no outgoing mail.

## What it does

A character can toggle the ruleset on itself with a chat command:

```
.selffound on | off | status
```

While a character is flagged Self-Found, the module blocks:

- **Trading** — the trade window will not open with another player (or bot).
- **Auction House** — the auction window will not open (browse, post, bid and buyout).
- **Outgoing mail** — the character cannot send mail.

Grouping and questing are unaffected. The flag is per character and reversible.

## Limitations

- Receiving mail is not blocked (another player can still mail items *to* a flagged
  character). Only the outgoing direction is closed, which covers self-trading between your
  own characters.

## Configuration

`conf/mod_self_found.conf.dist`:

| Key                            | Default | Description                          |
|--------------------------------|---------|--------------------------------------|
| `SelfFound.Enable`             | `1`     | Master on/off switch                 |
| `SelfFound.BlockTrade`         | `1`     | Block trading                        |
| `SelfFound.BlockMail`          | `1`     | Block outgoing mail                  |
| `SelfFound.BlockAuctionHouse`  | `1`     | Block the auction house              |
| `SelfFound.AnnounceOnLogin`    | `1`     | Remind flagged characters on login   |

The module creates its own storage table automatically on first start — no SQL file to
import, no client patch needed.

## Installation

Clone into your AzerothCore `modules/` directory and rebuild the worldserver.

## License

Released under the GNU GPL v2 (or later).
