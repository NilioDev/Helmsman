# Planets on the viewscreen (`PLANET.DAT`)

The planet of a star system is not stored as a picture. It is made from a
seed number and drawn as a turning ball.

## The surface

The surface is 128 x 128 heights, made by midpoint displacement with the
random number generator of Microsoft's C library, seeded with the
planet's number:

- `state = state * 214013 + 2531011`, and a random number is
  `(state >> 16) & 0x7FFF`.
- The four corners are 0. The top edge, then the left edge, then the
  square are split again and again.
- Each midpoint is the 16-bit mean of its ends, plus
  `(int16)(random - 0x4000) % (half width + half height)`.
- The heights are then scaled to 25 shades, in 16-bit arithmetic.

## Colours

Each shade is looked up in one of 22 colour ramps of 25 palette colours.
Helmsman reads the ramps from the player's own game program (see
`program.md`), at offset 0x34720 of its unpacked image.

The first 20 star systems have a fixed seed and ramp. Any other system
gets `seed = random % 1000` and `ramp = random % 16 + 6`.

## `PLANET.DAT`

- 0x0000: a turn table, 128 rows of 64 16-bit words.
- 0x4000, 0x40F2, 0x4464, 0x4BE6 and 0x5908: disc tables for discs 11,
  21, 31, 41 and 61 pixels across, n x n words each. An entry with bit 15
  set is outside the disc.

For a pixel of the disc, its entry plus the row for the planet's tilt
(`& 0x3FFF`) indexes the turn table. Adding the row for how far the planet
has turned (`& 0x3FFF`) gives the place on the surface.

A turn is turned into a row as `((angle << 7) + 0x200) / 0x400 & 0x7F`,
times 128.

## Size

The planet's radius is 500. The disc is picked from the planet's depth `z`
against `r * focal`: at least that gives 11 pixels, at least a half gives
21, a third 31, a quarter 41, and nearer 61.
