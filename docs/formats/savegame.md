# Saved games (`.hsav`)

Helmsman uses its own saved game files; it does not read or write the
original games' saves. Each save is one file in the player's save folder,
named `GAME-N.hsav` (`anniversary-3.hsav`).

All numbers are little-endian. A *text* is a 16-bit length and then that
many bytes. A *block* is a 32-bit length and then that many bytes.

| Part | Meaning |
|------|---------|
| 8 bytes | `HLMSAVE` and a zero byte |
| 32-bit | the format's number, now 1 |
| text | the game (`anniversary` or `judgment`) |
| text | the description the player typed |
| 2 x 32-bit | when it was saved, in seconds since 1970, low half first |
| ... | what the game saved |

What follows depends on where the game was saved:

- **On the ship**: the text `bridge`, then the voyage (the mission, the
  star system, the score, the mission's flags and the ship's state) and the
  ships in space.
- **On a planet**: the text `away`, the parts that last between rooms
  (inventory, options), a byte saying whether the voyage follows, the
  voyage if so, and then the mission: its name, the room, the room's tick
  count, the mission's state block (`room.md`), the room's code and data
  as they are, the actors in the room and the events waiting.

Every read is checked against the file's size. A file that does not fit
is reported as damaged, and nothing is loaded from it.

A save is written next to its file first and then renamed over it, so a
failed write never spoils the older save.
