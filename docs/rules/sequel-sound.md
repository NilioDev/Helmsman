# Music and sound effects in the second game

## Suites

The music comes in suites. A suite is two files with the same name: a
music file (`.AD`, the same kind as the first game's, see
[../formats/music.md](../formats/music.md)) and a table (`.SUI`). The
instruments for every suite are in `TREK2.AD` in the `DATA` archive.

Each episode has three suites in its own archive, named after the episode
with one more letter:

| Suite | Heard |
|-------|-------|
| `<episode>S` | on the bridge |
| `<episode>G` | on the away missions |
| `<episode>I` | once, when the episode's music is first heard |

The `I` suite comes first whenever the music changes to another episode
(or to the title); only after its song ends is the asked-for suite read. On
the AdLib card these songs are empty, so in practice the music goes straight
on. The title has `TITLE` and `TITLEI` in the `DATA` archive; `TITLE`'s
second song is the theme.

Changing suites stops the music. Nothing plays until the scripts ask for a
song.

## Asking for a song

The scripts ask with two numbers: the song to play now and the background
song, the one to go on with. In either place 0xFE means "no song" and -1
means "leave it as it is"; a "now" of -1 means the background.

- A song that is already playing is not started again.
- When the song now is not the background and the background is asked for,
  the song now plays to its end first.
- When a song ends, the background starts (or starts over, when it was the
  background that ended).

Turning the music off stops it; turning it on starts the background.

The space script's command 0x1D and the room function 0x11 ask for songs
this way.

## The table of a suite

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 8 × 10 | the suite's own sounds: eight names, zero-padded (empty when unused) |
| 80 | 2 | the number of sound effects |
| 82 | 12 each | the sound effects |

Each sound effect:

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 9 | the name of a sound in the sound banks, or empty |
| 9 | 1 | a flag (not used by Helmsman) |
| 10 | 2 | which of the suite's own sounds plays (0–7), or -1 for the named one |

The suite's own sounds are plain unsigned 8-bit files (`.RAW`, at 11025
samples a second) in the archives, like the phaser shots. The named sounds
are in the sound banks (see [../formats/sound-bank.md](../formats/sound-bank.md)).

## Playing a sound effect

The space script's command 0x1C and the room function 0x10 play effect
number `a` of the suite at volume `b` (0 to 127) and pan `c` (0 left, 64
the middle, 127 right). Effect 0xFE silences all effects.

## In saved games

The song asked for and the background are saved; the suite is read again
where the game goes on.
