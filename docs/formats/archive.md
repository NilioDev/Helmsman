# Archives (`.DIR`, `.RUN`, `.001`)

Both games keep almost all of their files in packed archives. An archive
named `BASE` is three files:

- `BASE.DIR`: the directory, a list of 14-byte entries.
- `BASE.RUN`: where the files of numbered runs start (see below).
- `BASE.001`: the packed files, one after another.

The first game has one archive, `DATA`. The sequel has `DATA` plus one per
mission (`FED`, `LIGHT`, `MADNESS`, `METH`, `NOMAN`, `SCOTTY`, `SENTNL`,
`VOIDS`).

## Directory entries

| Bytes | Meaning |
|-------|---------|
| 8 | name, upper case, padded with zero bytes |
| 3 | extension, the same way |
| 3 | a 24-bit little-endian value |

When bit 23 of the value is clear, the value is the offset of the file in
`BASE.001`.

When bit 23 is set, the entry stands for a *run* of files whose names count
up from the entry's name. Bits 16 to 22 give how many files the run has.
The low 16 bits give an offset into `BASE.RUN`. At that offset there is a
24-bit offset into `BASE.001` for the first file, followed by one 16-bit
step for each further file, each added to the previous file's offset.

Names in a run count up in the digits at the end of the name part
(`ADMIRA09` is followed by `ADMIRA10`). A name that ends in a letter
counts up in that letter.

## Packed files

Each file in `BASE.001` starts with two 16-bit little-endian sizes: the
size once unpacked, then the size of the packed data that follows.

The data is LZSS:

- A flag byte says what the next eight items are, lowest bit first. A set
  bit means one literal byte follows.
- A clear bit means a 16-bit little-endian reference `v` follows. It copies
  `(v & 15) + 3` bytes from `(v >> 4)` bytes back in the output.

Helmsman never reads outside the packed data. Data that ends early leaves
the rest of the file as zeroes, and so does a reference to before the
start.
