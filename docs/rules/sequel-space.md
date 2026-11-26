# Fights in space (second game)

The second game's fights are those of the first game (see
[space.md](space.md)): the same ships, parts, shields, weapons and
behaviour. What differs is how the space script starts them, the crew's
menus that give the orders, and what the program keeps in its data
segment. Addresses are `DS:xxxx` in the original program's data segment
(see [sequel-scripts.md](sequel-scripts.md)).

## How hard

A new game asks for the combat level, kept at `DS:00CC`:

| Level | Name | Fights |
|-------|------|--------|
| 0 | Federation Cadet | none: the scripts go round them |
| 1 | Academy Graduate | easy: the enemies come in their weaker form |
| 2 | Commissioned Officer | hard |

Leaving the menu with Escape picks 2. Where a script names a ship it often
gives two names one after the other ("fedpir", "fedpire"); below level 2
the second (weaker) one is used.

## The script's commands

| Command | Meaning |
|---------|---------|
| 1 `name, count` | `count` ships of class `name` come, each near the player's ship (up to 0x400 away in each direction, at random) |
| 2 `name, count` | a wave: ships of class `name` keep coming, so that `count` fight at once, until the script says otherwise |
| 3 `n` | ship `n` gives up: it flies away, and is gone once far enough off |
| 4 `n` | whether ship `n` (−2: our own) has lost more than half its hull; a destroyed ship counts as such |
| 5 `n` | whether ship `n`'s shields are below two thirds |
| 6 `n` | a still object leaves space |
| 0x10 | full speed ahead |
| 0x2B | shields up |
| 0x2C | weapons armed |

`DS:C220` says whether a fight goes on (any enemy ship still fighting).
When the player's ship is destroyed, the game-over menu opens.

## On the bridge

Space shows in the viewscreen when no one is on it. Above it are the bars
of the four weapons (filling as each comes ready again), the warp engines'
power and the speed; at the sides the pictures of the player's ship and
of the target, coloured by damage; and the radar on the helm. These are
drawn as in the first game, with the second game's colours.

The fight's state in the data segment:

| Place | Meaning |
|-------|---------|
| `DS:00C0` | the full view: space fills the screen |
| `DS:00C2` | manual control: the pointer steers the ship and can fire |
| `DS:00C8` | the magnification of the view (1 to 5) |
| `DS:00CA` | which way the view looks (0 ahead) |
| `DS:0104` | target analysis on |
| `DS:0108` | weapons locked on a target |
| `DS:010A` | the target locked on |
| `DS:010C` | the lock's strength: it grows to 50 while the target stays within 45° ahead |
| `DS:C47E` | shields up |
| `DS:C482` | weapons armed |
| `DS:BF70`, `DS:C84A` | a wave's size and its ships' class |
| `DS:6E96`, `DS:6E9E`, `DS:6EA0` | extra power: in use, spare power, how long it lasts |

Firing only works under manual control: the left button (or Enter) fires
the phasers, the right button (or Space) the photon torpedoes. A lock only
helps the aim.

The crew's buttons:

| Button | Order |
|--------|-------|
| 0x22 | target analysis on or off |
| 0x30 | damage control: the part repaired first |
| 0x31 | extra power for the engines (once in a while) |
| 0x50 | the speed menu: reverse, or 0 to 9 |
| 0x52 | shields up or down |
| 0x53 | the view menu: which way to look, magnification, the full view |
| 0x61 | weapons armed or not |
| 0x62 | lock the weapons on |

The keyboard has these too:

| Key | Does |
|-----|------|
| Tab | manual control on or off |
| W, S, L | weapons, shields, lock |
| A, D, E | target analysis, damage control, extra power |
| `[` `]` | slower, faster |
| `` ` `` | full speed backwards |
| `,` `.` | magnification |
| V | the full view |
| Enter, Space | phasers, photon torpedoes (manual control only) |
| R, C | the captain's log, the library computer (see [sequel-computer.md](sequel-computer.md)) |

## The viewscreen

Command 8 puts someone on the viewscreen: their animation, and their own
colours from the palette file of the same name (`RAYNOK.PAL` for
"tlkraynok"). Their palette replaces the bridge's colours 0x30–0x7F,
0xD8–0xDB and 0xE0–0xF5. Who is on it is kept at `DS:C00A` (8 bytes), so
the colours come back with a saved game. A song number other than −1
starts that song as the episode's music changes on screen.

## The admiral's rating

Command 0x13 ends an episode's score: the points earned (`DS:C224`) out
of those possible (`DS:C222`), as a percentage:

| Percent | Rating | Song |
|---------|--------|------|
| 50 or less | 0 | 9 |
| up to 70 | 1 | 9 |
| up to 85 | 2 | 0x0C |
| up to 99 | 3 | 0x0E |
| 100 | 4 | 0x0A |

When the command's argument is not 0, Admiral Cain says so on the
viewscreen (`WOMAN.ANM`, `WOMAN.PAL`), her words (`EVAL000` and one of
`EVAL001`–`EVAL005`) in a box at the foot of the screen.

The rating is kept for the episode at `DS:6032 + 2 × episode` and added
to `DS:2658`; `DS:C226` becomes that sum as a percentage of the most
possible (four per episode so far). It also makes the ship stronger for
the rest of the game: spare power `+ rating/2`, extra power's duration
`+ rating × 15`, each shield's best `+ rating/2` and each weapon's best
`+ rating/4`. The power figures stay in the data segment; the shields and
weapons Helmsman sets again from the kept ratings whenever an episode
starts.
