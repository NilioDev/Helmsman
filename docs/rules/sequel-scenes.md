# Scenes of the second game

The second game strings its films (see [../formats/movie.md](../formats/movie.md))
and painted animations (see [../formats/animation.md](../formats/animation.md))
together with small scene scripts, `.DEM` files in the `DATA` archive.
`TITLE.DEM` is the opening; each episode has its own for its start and end.

## Scene scripts

A script is a list of 16-byte records: the frame (0xFFFF: none), a command,
two numbers `a` and `b`, and a name of 8 characters padded with spaces.

| Command | Meaning |
|---------|---------|
| 0 | play the painted animation `UNCOMP/name.ANM` |
| 2 | nothing |
| 5 | play the sound `name` from `DATA.VCC` |
| 6 | `a` = 0: go on; 1: start the script over; 2: the end |
| 7 | fade speed `a` (percent per clock tick) |
| 8 | play the film `UNCOMP/name.MVE` |

Commands 1, 3 and 4 appear in other scripts and are still to be described.

The records run in order. While an animation plays, the script is asked
again as each frame comes up: only a record for exactly that frame runs then
(such as the sound of the logo at frame 1). Once nothing is playing all
records run in turn, but an animation, a film or the end only start then.

A key or mouse click skips: the script jumps ahead to its next end record
(command 6 with `a` other than 1), stopping what plays.

## Timing

An animation shows its frames at the rate in its header. It fades in from
black at its start and out at its end, 10% of the brightness per tick of the
PC's clock (18.2 ticks a second). A film shows each picture for the time its
timer gives, centred on the screen.
