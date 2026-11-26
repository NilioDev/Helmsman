# Sound banks (`.VCC`)

The second game keeps its spoken lines and many sound effects in banks:
`DATA.VCC`, `BACKSND.VCC`, `COMPUTER.VCC` and one per mission.

All numbers are little-endian.

## Bank

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 8 | `VOCFILES` |
| 8 | 4 | where the list starts |
| 12 | 4 | number of sounds |

The list has 16 bytes per sound: the name (8 bytes, zero-padded), where the
sound starts and how many bytes it takes. A few sounds are plain VOC files
(see [voc.md](voc.md)); the rest are packed.

## Packed sound

The header is 26 bytes: `Interplay ACMP Data`, the byte 0x1A, the sample
rate (16 bits) and four more bytes. Then a 32-bit count of samples, then the
packed data. The result is unsigned 8-bit mono, 0x80 being silence.

### Two readers on one stream

The packed data is read by two readers that take bytes from the same stream,
each when it runs out:

- The field reader gives `n` bits at a time, lowest bit first.
- The run reader gives the length of the next run of equal bits, starting
  with a run of ones, then of zeros, and so on. Each run ends at (and uses
  up) the first bit that differs; that bit counts as the first bit of the
  next run, so every length is at least 1. A length of exactly 10 means a
  6-bit field follows with the real length.

Both readers start afresh at each block: bits left in their bytes are
dropped.

### Blocks

Samples come in blocks of 256 (the last one shorter). Each block starts
with a field of 8 bits:

- bits 0–4: the method,
- bits 5–6: a shift `s`,
- bit 7: only every other sample is stored; the rest are filled in.

Most methods keep a level that starts at 0x80 in each block; each sample
adds `delta × 2^s` to it and the sample is the level's low byte. "Signed
from the low bit" turns a number `v` into `-(v >> 1)` when `v` is odd, else
`v >> 1`.

| Method | Each sample |
|--------|-------------|
| 0 | one 8-bit field gives the value for the whole block |
| 1 | `w` = run − 1; delta = 0 when `w` is 0, else signed from the low bit of (field(`w`) with bit `w` set) |
| 2 | `d` = field(4); delta = `d − 8`, or when `d` is 0 a field of `8 − s` bits |
| 3 | delta = signed from the low bit of field(3) + (run − 1) × 8 |
| 4 | delta = signed from the low bit of field(4) + (run − 1) × 16 |
| 5 | `d` = field(2): 0 and 1 are the delta, 3 means −1, 2 means `w` = run and delta = signed from the low bit of (field(`w` + 1) with bit `w + 1` set) |
| 6 | the sample is field(`8 − s`) shifted up by `s` |

### Filling in

When bit 7 is set the block stores `(count + 1) / 2` samples. Stored sample
`k` goes to place `2k + 1`. Place `2k` lies between stored `k − 1` (for
`k` = 0: the last sample of the block before, 0x80 at the start) and stored
`k`:

- with two stored samples on each side (`1 < k < stored − 1`): with
  `a` = stored `k − 1` + stored `k` and `b` = `a` − stored `k + 1` − stored
  `k − 2`, the sample is `(3b + 32(a + 1)) >> 6`, kept within 0..255;
- otherwise the mean of the two neighbours, the left one first rounded up to
  even.

Helmsman refuses a run longer than 64 bits; bytes past the end read as zero.
