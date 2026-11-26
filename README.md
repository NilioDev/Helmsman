# Helmsman

Play two 1990s starship adventure games on today's computers, from your own
CDs. Helmsman plays **Star Trek: 25th Anniversary** (the enhanced CD-ROM
edition) and **Star Trek: Judgment Rites** (the CD-ROM edition). It is not
an emulator: it is a new program that reads the files on your CDs and plays
the games with them, in a window of any size, with a mouse, a keyboard or a
gamepad.

Helmsman does not contain the games. You need your own copies.

## What you need

- Your CD of either game, or both: the disc itself copied to a folder, or
  an image of it (a `.cue` with its `.bin` files, a single `.bin`, or an
  `.iso`).
- Windows, macOS or Linux.

## Getting Helmsman

There are no ready-made downloads yet. To build it yourself, see
[CONTRIBUTING.md](CONTRIBUTING.md#building); it takes CMake and a C++
compiler, and fetches everything else by itself.

## Adding your games

Start Helmsman. It shows a card for each game. To add one, drop its CD
image (the `.cue` or `.iso`) or the CD's folder onto the window. Helmsman
remembers it from then on.

You can also put the images or folders in a folder named `gamedata`, next
to Helmsman or in its settings folder:

- Windows: `%APPDATA%\NilioDev\Helmsman\`
- macOS: `~/Library/Application Support/NilioDev/Helmsman/`
- Linux: `~/.local/share/NilioDev/Helmsman/`

Or give the paths on the command line: `Helmsman path/to/game.cue`.

## Choosing a game

Point at a card and click it, or use the arrow keys and Enter (`1` and `2`
pick a game at once). When a game ends, or you quit it, you come back to
this screen.

## Playing

The games play with the mouse, as they always did: the left button acts,
the right button opens the menu of actions on a planet. The keyboard
shortcuts of the original games work too.

### On the bridge

Click an officer for their orders, or use the keys:

| Key | 25th Anniversary | Judgment Rites |
|-----|------------------|----------------|
| W | arm or disarm the weapons | arm or disarm the weapons |
| S | shields up or down | shields up or down |
| A | target analysis | target analysis |
| D | damage report | damage control: what to repair first |
| E | the repair menu | extra power for the engines |
| H | hail | hail |
| T | ask Spock | Spock's report |
| C | the library computer | the library computer |
| K | Kirk's menu | Kirk's menu |
| L | the captain's log | lock the weapons on a target |
| R | | the captain's log |
| O | standard orbit | standard orbit |
| N | the star map | the star map |
| X | the transporter | |
| B | | beam down |
| V | the viewscreen | the full view of space |
| I | | the options |
| Tab | steer with the pointer | steer with the pointer |
| Enter or F1 | fire phasers | fire phasers |
| Space or F2 | fire photon torpedoes | fire photon torpedoes |
| `[` `]` | | slower, faster |
| 1 to 9, 0 | speed (1 stops, 0 is full) | |
| `` ` `` | full speed backwards | full speed backwards |
| `,` `.` | zoom out, in | zoom out, in |
| F3 | steer with the pointer | look ahead |
| F4 to F8 | | look behind, left, right, up, down |
| Esc | the options | the options |

In a fight you fire at the point you click in the viewscreen, once you
steer with the pointer and the weapons are armed.

### Judgment Rites: how hard the fights are

A new game of Judgment Rites asks for the combat level: *Federation
Cadet* (no fights), *Academy Graduate* (easy) or *Commissioned Officer*
(hard). At the end of each episode the admiral rates how well you did; a
good rating makes the Enterprise a little stronger for the episodes after.

### Typing

The library computer and a few puzzles ask you to type a word. Type it on
the keyboard, or click the letters shown on the screen ("Back" takes a
letter away, "Done" answers).

### With a gamepad

The gamepad moves the pointer: the left stick moves it freely, the
direction pad jumps to the next thing you can click. A and X click, B and
Y right-click, Start opens the options. You can choose what each button
does; see [docs/gamepad.md](docs/gamepad.md).

## Saving

Save and load from the options (Esc, or Kirk's menu). Saved games are kept
in the `saves` folder inside Helmsman's settings folder (above). Judgment
Rites also saves by itself at the start of each episode.

## Sound

Music plays as it did on an AdLib card, and the CD's speech is heard.
Sound effects and music can be turned off in the options.

## State of play

- **25th Anniversary**: every mission can be played to its end.
- **Judgment Rites**: all its parts are in (the bridge, fights, the away
  missions, speech, music, the library computer); the eight episodes are
  being played through one by one, so you may still meet a spot that does
  not work yet.

## Licence

Helmsman is free software under the GNU General Public License, version 3
or later (see [LICENSE](LICENSE)). The games themselves belong to their
makers and are not part of Helmsman.
