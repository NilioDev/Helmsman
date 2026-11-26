# Painted animations (`.ANM`)

Most `.ANM` files in the second game's `UNCOMP` folder are full-screen
animations in the "LPF" layout of a well-known paint program. A few `.ANM`
files are films instead (they start like a `.MVE`, see [movie.md](movie.md)).

All numbers are little-endian.

## Header (first 256 bytes)

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 4 | `LPF ` |
| 4 | 2 | most pages the file may have (256) |
| 6 | 2 | number of pages |
| 8 | 4 | number of records: the frames plus one that leads from the last frame back to the first |
| 12 | 2 | most records in a page (256) |
| 14 | 2 | where the page table starts (0x500) |
| 16 | 4 | `ANIM` |
| 20 | 2 | width |
| 22 | 2 | height |
| 29 | 1 | packing (1: the run, skip and copy codes below) |
| 64 | 4 | number of frames |
| 68 | 2 | frames per second |

From 0x80 come colour cycles of 8 bytes each; in the game's files they
all have rate 0, so nothing cycles.

## Colours (0x100)

256 colours of four bytes: blue, green, red (8 bits each), and a spare byte.

## Page table (0x500)

256 entries of six bytes: the first record in the page, how many records
it holds, and how many bytes they take. Entry `i` describes the page at
`0xB00 + i × 0x10000`. The entries are not in record order, so a reader
looks up the page whose range holds the record.

## Pages

A page starts with its own copy of the first record and record count, the
byte count, and a zero word. Then one 16-bit size per record, then the
records one after another.

A record of size 0 leaves the picture as it is. Otherwise it starts with
`B`, a count of extra words (skipped), and a word, then drawing codes that
change the frame before (the first frame is drawn over black):

| Code byte `c` | Meaning |
|---------------|---------|
| 1–0x7F | copy the next `c` bytes |
| 0 | a count byte and a colour: that many pixels of the colour |
| 0x81–0xFF | skip `c − 0x80` pixels |
| 0x80 | a word `w` follows: 0 ends the record; below 0x8000 skips `w` pixels; with 0xC000 set, a colour follows and `w & 0x3FFF` pixels get it; else copy the next `w & 0x3FFF` bytes |

Pixels go left to right, top to bottom, through the whole picture. Helmsman
refuses a record that would draw past the end.
