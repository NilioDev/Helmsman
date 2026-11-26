# Planet rooms (`.RDF`)

Each room of a planet mission is a file named after the mission and the
room's number (`DEMON0.RDF`, `DEMON1.RDF`...). It holds the room's layout
and the room's own program. The program is 16-bit x86 code, which
Helmsman runs in its own small interpreter (`src/core/src/cpu`). The
code calls the game for everything it does.

Helmsman loads the whole file at offset 0 of a 64 KB segment of its own.
A file smaller than 0xCA bytes or larger than 0xFFF0 bytes is refused.

## Header

All values are 16-bit little-endian; an offset is from the start of the
file. Each "list" is given as a start and an end offset.

| Offset | Meaning |
|--------|---------|
| 0x00 | a far pointer the code calls the game through (filled in on load) |
| 0x04 | where the mission's state block is (filled in on load) |
| 0x06 | the lowest row the crew stands on (signed) |
| 0x08 | the highest row (signed) |
| 0x0A, 0x0C | the scale at the highest and lowest row: the crew is drawn larger the lower they stand |
| 0x0E, 0x10 | the action list (below) |
| 0x12, 0x14 | the things that can be pointed at |
| 0x16, 0x18 | the ways out of the room (polygons) |
| 0x1A, 0x1C | places the crew sets something off by walking onto (polygons) |
| 0x1E, 0x20 | names of the room's background animation files (`.BAN`), each ending in a zero byte |
| 0x22 | for each way out, the room it leads to |
| 0x2A | where the crew walks in from and to, 32 bytes for each way in (4 crew x from-x, from-y, to-x, to-y) |
| 0xAA | where the crew appears when beaming in, 4 x (x, y) |
| 0xBA | where the crew stands when a saved game starts here, 4 x (x, y) |

A way in matches the way out it is the other side of: going out through
way `n` brings the crew in through way `n ^ 1` of the next room.

## Polygons

A polygon is an id word, a count of corners, then that many (x, y)
pairs. The next polygon follows directly.

Things that can be pointed at use the same polygons, except that an id
with bit 15 set is followed by the place the crew walks to before acting
on it (x, y), and then the polygon. In that case the low bits of the id
say which actions (walk, use, get, look, talk) the walk applies to.

## The action list

Each entry is:

| Bytes | Meaning |
|-------|---------|
| 4 | the event the entry answers: action, then three bytes saying with what and on what |
| 2 | offset of the next entry |
| ... | the code that answers it, which returns with a far return |

The actions are 0 tick, 1 walk, 2 use, 3 get, 4 look, 5 talk, 6 touched
a way out, 7 touched a place, 8 a timer ran out, 10 an animation ended,
12 a walk ended, 13 options. When the code sets the state block's "not
handled" byte, the game goes on looking, and finally answers the way it
answers anything (for example "Nothing happens.").

## Calling the game

The code calls the game with `call far [cs:0]` after pushing its
arguments; the first pushed word is the function's number. Helmsman's
`Mission::roomFunction` answers them; each one is described there. Some
examples:

- 0x13: the player types an answer. The argument points at up to 30
  bytes of answers, each ending in a zero byte, with an empty one last.
  The result is the number (from 1) of the answer that matches, ignoring
  case, or 0.
- 0x14: the turbolift's map in the last mission. The second argument
  says which lift the crew is in. The player points at a deck, and the
  crew goes to the room for it.

## The mission's state

0x129 bytes that all rooms of a mission share, and that saved games keep.
Some offsets:

| Offset | Meaning |
|--------|---------|
| 0x00 | eight 16-bit timers, counted down each tick |
| 0x10, 0x12 | the mouse's x and y |
| 0x14 | four 16-bit counts, one for each of the crew: how long they stay down when knocked down |
| 0x1C | the crew cannot walk |
| 0x1D | the crew is coming in |
| 0x1E | the security officer is dead |
| 0x1F | the action chosen |
| 0x20, 0x21 | the thing used, and the thing it is used on |
| 0x23 | not handled |
| 0x24 | the crew knocked down, one bit each (Kirk 1, Spock 2, McCoy 4, security 8); ways out work only while this is 0. Each tick, a knocked-down member's count goes down by one; at 0 they get up (their `getu` animation, facing the way 0x25 says) and their bit is cleared |
| 0x25 | the four crew members' facings (0 to 3 for n, s, e, w; 0xFF: as they stand) |
| 0x29 | the turbolift's picture was shown (last mission) |

The rest belongs to the missions' own code.
