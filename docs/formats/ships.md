# Ships (`.DSC`) and ship pictures (`.SHP`)

## Ship descriptions (`.DSC`)

One file for each kind of ship (`ENTERPRI.DSC`, `KLINGON.DSC`...). All
numbers are little-endian; "16.16" numbers have 16 bits of fraction.

| Bytes | Meaning |
|-------|---------|
| 2 | which set of pictures: 0 the player's ship, 1 Klingon, 2 Orion pirate, 3 Romulan |
| 2 | size (how near a shot must come) |
| 2 | picture scale |
| 6 x 6 | the six shields: works (0 or not), best strength, strength now |
| 10 x 6 | the ten parts, the same way: hull, bridge, port and starboard torpedoes, port and starboard phasers, sensors, impulse, port and starboard warp |
| 4 | most turn a tick (16.16) |
| 4 | how fast turning changes (16.16) |
| 4 | top speed (16.16) |
| 4 | how fast speed changes (16.16) |
| 0x32 | the ship's moving state, all zero in the files |
| 4 | the distance it likes to fight at |
| 2 | it can cloak (0 or not) |
| 4 x 22 | four weapons (below) |

Each weapon: when it may fire next (4 bytes, zero in the files), how long
its shot lasts, how fast the shot flies, how long it takes to be ready
again (2 bytes each), where it sits on the ship as x, y and z (4 bytes
each), and what it fires (2 bytes: 0 torpedo, 1 phaser, 2 plasma).

A file too short for all of this is refused.

## Ship pictures (`.SHP`)

A `.SHP` file is a picture in the format of `pictures.md`. Each ship has
one picture for each angle it can be seen at. `STARS.SHP` is the
exception: it holds the sixteen star pictures one after the other.
