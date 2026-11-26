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

For the second game (`judgment`) the part after the header is:

| Part | Meaning |
|------|---------|
| byte | the layout's number, now 3 (1 had no ships and no music, 2 no music) |
| byte | where: 0 on the ship, 1 on a mission |
| block | the whole data segment of the original program, 0x10000 bytes: the scripts keep everything they know there |
| ships | the things in space (below) |
| 32-bit | the song asked for, or -1 |
| 32-bit | the background song, or -1 |

The things in space are a byte for each of space's places: 0 when it is
empty, otherwise its kind (2 a ship, 1 a still object) followed by its name
(text), where it is (three 32-bit numbers), which way it faces (nine 16-bit
numbers) and, for a ship, each part and each shield (whether it works, a
byte, then its best and its strength now, 32-bit each), the speed it wants (32-bit), whether its shields are
up and its weapons armed (a byte each) and what it is doing (byte).

On a mission the room follows: the room's number (32-bit); three texts, the
names of the room picture, the depth map and the walking map shown now (a
room may change them); the brightness and the brightness aimed at (percent,
32-bit each); the colour fades go to (three bytes); the room's tick count;
the touch area Kirk stands in and the exit about to be taken (32-bit each);
whether the crew may not take orders (byte); the events waiting (a 32-bit
count, at most 16, of four bytes each); then for each of the 32 people a
byte saying whether they are in the room and, when they are, their
animation's name (text), its picture, the ticks to the next picture, where
they stand, their size, whether they walk, the corners still to walk to (a
32-bit count, at most 64, of two 32-bit numbers), the walk's place and step
(16.16), the steps left, the walking picture, the way they face (byte), the
code they report when done, whether the last picture stays (byte), how they
are drawn (byte) and their drawing order.

The second game's episodes save by themselves at their start (the space
script's checkpoint, see [../rules/sequel-scripts.md](../rules/sequel-scripts.md)): such a save is named
`judgment-checkpoint-EPISODE-0.hsav` and is not listed among the player's
saves.

Every read is checked against the file's size. A file that does not fit
is reported as damaged, and nothing is loaded from it.

A save is written next to its file first and then renamed over it, so a
failed write never spoils the older save.
