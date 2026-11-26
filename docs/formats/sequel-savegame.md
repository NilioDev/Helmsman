# Saved games of the second game

The second game saves by writing blocks of its data segment one after the
other, in a fixed order; some blocks are only there when a value read
before says so. `SAVEGAME.NEW` on the CD is a saved game too: the start of a
new game.

All numbers are little-endian. "`DS:xxxx`, n" means n bytes that belong at
that offset of the original program's data segment.

## Order

1. A description, 0x19 bytes.
2. The version, 2 bytes: 0x24.
3. The game state `DS:6E0A`, 2 (see [../rules/sequel-scripts.md](../rules/sequel-scripts.md)).
4. Ships: our own `DS:C33A`, 0x1CE; `DS:BBBA`, 0xA8; `DS:C032`, 0x0C;
   `DS:71A4`, 0x12; `DS:C84A`, 0x0A; `DS:BF70`, 2; then four ships from
   `DS:B446` on, 0x1CE each.
5. `DS:851A`, 4; `DS:00C2`, 2; `DS:00C4`, 2; `DS:C0A6`, 2; `DS:BC62`, 4;
   `DS:00C6`, 2; `DS:00CA`, 2; the episode's name `DS:C83A`, 0x10;
   `DS:6E96`, 0x0C.
6. The space script: its resume point `DS:5EBA`, 2; `DS:5EBE`, 2;
   `DS:C00A`, 0x0A; `DS:2634`, 2; the episode number `DS:2636`, 2;
   `DS:6032`, 0x10; `DS:2658`, 2; `DS:2630`, 2. When `DS:2630` is not 0:
   `DS:5EC0`, 0x12A and `DS:5FEA`, 2.
7. When the state is 2 (on a mission): the room (`DS:B434`, 0x10 and more,
   still to be listed).
8. `DS:851A`, 4 again.
9. When the state is not −1: the 32 people and things of the scene,
   `DS:73D8`, 0x1140 (0x8A each; the first eight are the crew).
10. The scripts' block `DS:C202`, 0x12A; `DS:C7BA`, 0x40; `DS:9AF2`, 2;
    `DS:B418`, 2; `DS:11DA`, 0x280.
11. Music and sound: the song's name and more, still to be listed.
