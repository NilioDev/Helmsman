# Films (`.MVE`)

The second game shows its films (the opening, ship flights, endings) from
`.MVE` files in the `UNCOMP` folder of the CD. These notes come from looking
at those files and checking every reading against the pictures it gives.

## Layout

- The file starts with `Interplay MVE File`, the byte 0x1A, and more header
  bytes up to offset 26.
- Then chunks: a 16-bit length and a 16-bit kind, then that many bytes.
- A chunk holds operations: a 16-bit length, a type byte and a version byte,
  then that many bytes.

All numbers are little-endian.

| Type | Meaning |
|------|---------|
| 0 | the film ends |
| 1 | the chunk ends |
| 2 | timer: a 32-bit count and a 16-bit step; each picture stays count × step microseconds |
| 3 | sound set-up: a word we skip, flags (bit 0 stereo, bit 1 16-bit, bit 2 packed), the rate, a buffer size |
| 5 | picture set-up: width and height in 8×8 blocks (more words follow in later versions) |
| 6 | a picture, every block coded |
| 7 | show the picture |
| 8 | sound: a running number, a mask of sound tracks, a length, then the samples |
| 9 | silence: the same three words, no samples |
| 12 | colours: first colour, count, then three 6-bit values per colour |
| 14 | skip list for the next type 16 picture |
| 15 | block codes for the next type 16 picture |
| 16 | a picture where only some blocks are coded |

Helmsman plays sound track 0 (bit 0 of the mask). The films seen so far
have unpacked 8-bit mono sound at 11025 Hz; only the opening has any.

## Pictures

There are two picture buffers that take turns. A new picture is drawn over
the one shown before last, and may copy from the one shown last. After it
is drawn the two swap roles.

Both picture types start with seven words: picture number, number of the
picture before, two zero words, the width and height in blocks, and flags.

Each block has a 16-bit code:

- 0: 64 raw pixels follow in the pixel data, eight rows of eight.
- High bit clear: copy 8×8 pixels from the picture being drawn, starting
  `code - 0x4000` pixels away. The distance is counted along the rows
  (`y × width + x`), so it can reach other rows. Code 0x4000 leaves the
  block as it is.
- High bit set: the same, but from the picture shown last, `code - 0xC000`
  pixels away.

Raw blocks are filled in first, then the copies in block order, left to
right and top to bottom. A copy that would read outside the picture is left
out.

### Type 6

After the seven words come one code per block, then the raw pixels.

### Type 16

The codes come from the last type 15 operation, one per block that changes.
Which blocks change comes from the last type 14 operation: 16-bit words,
read from the highest bit down. A set bit means the block changes. Each word
gives at most 15 blocks: its lowest set bit only marks where its blocks end,
and when nothing but that mark is left the next word is read. Blocks that do
not change keep what the buffer held (the picture before last). After the
seven words come the raw pixels.
