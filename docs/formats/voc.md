# Sounds and voices (`.VOC`)

The spoken lines (in the `VOC` folders of the CD) and many sound effects
are Creative Voice Files.

- The file starts with `Creative Voice File`, the byte 0x1A, and a 16-bit
  offset to the first block.
- Each block is a type byte and a 24-bit length, then that many bytes.
  Type 0 ends the file.

The blocks Helmsman uses:

| Type | Meaning |
|------|---------|
| 1 | sound with a rate byte and a codec byte, then the samples |
| 2 | more samples at the last rate |
| 3 | silence: a 16-bit count of samples (plus one) and a rate byte |
| 5 | text, ending in a zero byte; in the voices it holds the mouth shapes for the speaking face |
| 6 | a repeat starts: a 16-bit count of extra plays, 0xFFFF for "until stopped" |
| 7 | the repeat ends |

Other blocks are skipped. The sample rate for a rate byte `b` is
`1000000 / (256 - b)`.

## Samples

Codec 0 is unsigned 8-bit samples, 0x80 being silence.

Codec 1 is 4-bit packed. The first byte is the first sample. Each further
byte holds two steps, high nibble first. A step's low three bits are its
size, its top bit its direction. The step moves the sample by
`size << level`, plus `1 << (level - 1)` when the level is above 0, and
the sample stays within 0 to 255. After a step of size 5 or more the level
rises (up to 3); after a step of size 0 it falls (down to 0).
