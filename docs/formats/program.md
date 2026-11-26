# The game program (`STARTREK.EXE`)

A few tables live only in the game's program, not in its archives. Helmsman
reads them from the player's own copy, never from a copy of its own.

The program is a DOS MZ executable packed with Microsoft's EXEPACK:

- The header's code segment, times 16, gives the unpacker's block in the
  load image. Its signature `RB` sits at +14 of the block (or at +16 in
  the longer kind of block).
- The 16-bit word at +12, times 16, is the unpacked size.
- Unpacking runs backwards from the end of the packed image. Trailing
  0xFF bytes are skipped. Each command byte is followed (going backwards)
  by a 16-bit count:
  - `B0` or `B1`: fill `count` bytes with the next byte.
  - `B2` or `B3`: copy `count` bytes.
  - An odd command is the last one.

Every step is checked against the sizes. A damaged program gives an error,
and nothing is read outside it.

What Helmsman takes from the image:

- the planets' colour ramps (offset 0x34720, see `planets.md`).
