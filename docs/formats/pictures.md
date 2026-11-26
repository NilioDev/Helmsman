# Pictures, palettes and animations

## Pictures (`.BMP`)

Not the Windows format. A picture is:

| Bytes | Meaning |
|-------|---------|
| 2 | x of the handle (signed) |
| 2 | y of the handle (signed) |
| 2 | width |
| 2 | height |
| width x height | pixels, row by row, one palette index each |

The handle is the point of the picture that is placed on the position it
is drawn at. Colour 0 is see-through for sprites.

## Palettes (`.PAL`)

768 bytes: 256 colours of red, green and blue, each 0 to 63 (the VGA's six
bits). Helmsman scales each to 0 to 255.

## Animations (`.ANM`)

A list of 22-byte frames:

| Bytes | Meaning |
|-------|---------|
| 10 | the frame's picture name, padded with spaces or zero bytes |
| 2 | x offset from the actor's position (signed) |
| 2 | y offset (signed) |
| 2 | added to the depth of the actor's row (signed) |
| 2 | how many ticks the frame shows |
| 4 | four frame numbers, one of which follows, chosen at random |

## Scaling

Pictures on a planet are drawn larger or smaller by how deep in the scene
they stand, at `scale / 256` of their size. Columns and rows are picked
the way a line is drawn (a Bresenham walk). For rows, the first and last
row always show.
