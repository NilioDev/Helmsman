# How the second game runs

These notes describe how the second game's program is organised, as far as
Helmsman needs it. Addresses (`DS:xxxx`) are offsets in the original
program's data segment, which also is the layout of its saved games
(see [../formats/sequel-savegame.md](../formats/sequel-savegame.md)).

## Game states

The program runs one of five states, `DS:6E0A`:

| State | Meaning |
|-------|---------|
| 1 | on the bridge |
| 2 | an away mission (rooms) |
| 3 | beaming down |
| 4 | beaming back up |
| 5 | the episode is over: on to the next one |

A new game loads `SAVEGAME.NEW` from the CD; its state is −1, which loading
turns into 1 (the bridge).

The episodes are, in order: `fed`, `sentnl`, `noman`, `light`, `voids`,
`scotty`, `madness`, `meth`. The number of the current one is `DS:2636`, its
name `DS:C83A`. State 5 counts on to the next and, after the eighth, ends
the game. Starting an episode opens its sound bank (`<name>.VCC`) beside
`DATA.VCC` and `BACKSND.VCC`, sets up the crew afresh and loads the episode's
space script.

## Scripts

Three kinds of RDF file hold 16-bit x86 code (see
[../formats/room.md](../formats/room.md) for the first game's rooms):

| Word at 6 | Kind |
|-----------|------|
| 0x0111 | a room |
| 0x0211 | the shared code of a mission, and `DATA.RDF` |
| 0x0311 | an episode's space script, `<name>S.RDF` |

Code calls the game with `call far [cs:0]`; the function number is the
last thing pushed, the arguments above it.

All script code runs with DS and ES set to the program's data segment and
BX = SI = `C202`, a block of 0x12A bytes the scripts keep their own values
in. Room and mission code answer events (as in the first game); the space
script is one long program.

### The space script as a coroutine

The space script starts at offset 0x0C of its file the first time the
bridge is idle (its resume point, `DS:5EBA`, is 0). It runs on until it
asks for something that takes time, then gives up control:

- The game notes where it was called from (the return address in the
  script), adds 3 to step over the `add sp, n` after the call, and keeps
  that as the resume point.
- It leaves the script for good: the script's stack is dropped, so it must
  be empty at that call. The original stops with an error otherwise.
- Later the game starts the script afresh at the resume point.

Some commands give up control this way and go back to the bridge's own
loop (Pause, BeginEventLoop); others switch the game state (the title of an
episode goes to the bridge, OffShipAdventure goes to state 2 or 3, the end
of an episode to state 5).

The space script's commands are numbered 0 to 0x3A. Those known so far:

| Command | Meaning |
|---------|---------|
| 0 | the episode's title sequence; then to the bridge |
| 3 `n` | mark ship `n` as done |
| 4 `n` | whether ship `n` (−2: our own) has lost more than half its hull |
| 7 | the closing titles and the end of the game |
| 0x0C `n` | sets `DS:C22A` |
| 0x17 `n` | Pause for `n` (0: 1) |
| 0x18 | BeginEventLoop: wait for events on the bridge |
| 0x19 `beam, room, spawn` | OffShipAdventure: go down to a room (state 3 when `beam` is 0, else straight to state 2) |
| 0x1A | the episode is over |
| 0x1B `lo, hi` | a random number from `lo` to `hi` |
| 0x1C `song, a, b` | music |
| 0x33 `name` | checkpoint: the game is saved as `name` (the original writes `NAME.SAV`), with `DS:4FF8` set to 0 first; the script goes on at the next step, as after a Pause of 1 |
| 0x34 `name, code` | back to the checkpoint `name`; `DS:4FF8` is then `code` |
| 0x35 | `DS:4FF8`: 0 after a checkpoint is just made, the code it was come back to with otherwise (the script replays the episode's start when not 0) |
| 0x38 `who, text` | a text box with a spoken line |
| 0x39 `name` | open a sound bank |
| 0x3A | close the sound banks |
