# How Helmsman is built

Helmsman is a new program that plays the games from the player's own
files. It contains nothing of the originals: every format is read as
`docs/formats/` describes, and every rule is written down in our own words
in `docs/rules/`.

## The parts

```
src/app        the SDL3 window, sound, mouse, keys and gamepad
src/core       everything else, with no SDL: it builds and is tested
               without a screen
  data         reading the player's copy: CD images, archives, sounds,
               the game program
  gfx          pictures, palettes, fonts, the 320x200 screen
  audio        the music (Extended MIDI on an emulated FM chip)
  cpu          a small 16-bit x86 interpreter for the rooms' own code
  away         the planet missions: rooms, the crew, conversations,
               menus, saved games
  bridge       the ship: the bridge, the star map, the missions' story on
               the ship
  space        space as the viewscreen shows it, and ship fights
  engine       the choice of games, the titles, the gamepad, playing a
               game from start to end
tools          helmsman-dump, for checking the readers and the rooms
tests          unit tests; tests that need a game skip without it
third_party    Nuked OPL3 (the FM chip)
```

## How a game runs

The original games are long programs that wait in many places: in the
middle of a conversation, a fade or a video. Helmsman keeps that shape. The
game code runs on a thread of its own and yields whenever it waits for
time to pass. The app resumes it once a frame, after giving it the clock
and the input, and then shows the screen it drew and plays the sound it
queued. Only one of the two threads runs at a time, so they share nothing
that needs a lock, and the same input always gives the same game.

`Platform` (`engine/platform.hpp`) is everything the game sees of the
computer: the time, the mouse, the keys, the 320x200 screen and palette,
and queues of sound.

## The rooms' code

Each planet room carries its own program as 16-bit x86 code (see
`formats/room.md`). Helmsman runs it in `cpu/x86.cpp`, which knows only the
instructions the rooms use. When the code calls the game, the interpreter
stops and `Mission::roomFunction` does what was asked, in our own code.

## Reading outside files

Every reader takes its input as bytes and checks every length and offset
before it uses it. Damaged input gives a `FormatError`, never a read
outside the data. See `SECURITY.md`.

## Checking the games

`helmsman-dump` (in `tools/`) can list and unpack the archives, write
pictures as PNG files, play a room with scripted input, run every event a
room answers (`events`), and look for a way through a whole mission by
itself (`explore`). Run it without arguments for its commands.
