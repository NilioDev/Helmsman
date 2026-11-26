# Away missions in the second game

An away mission is a set of rooms. Each room is an RDF file of code and
tables (word 6 is 0x0111). The mission's shared code (0x0211) and `DATA.RDF`
answer what a room leaves alone. How the code is run is in
[sequel-scripts.md](sequel-scripts.md).

## The room file

All offsets are in the room's segment. Each "first, end" pair is two words:
where a table starts and where it stops.

| Offset | Meaning |
|--------|---------|
| 0x08 | the lowest point people walk to (y) |
| 0x0A | the highest |
| 0x0C, 0x0E | how large people are drawn at the top and at the bottom |
| 0x10 | the room's lists of handlers |
| 0x12 | first, end: the things to point at (polygons) |
| 0x16 | first, end: the ways out (polygons) |
| 0x1A | first, end: the touch areas (polygons) |
| 0x1E | first, end: the places nobody may walk |
| 0x22 | first, end: how the crew comes in, for each way in |
| 0x26 | for each way out, the room it leads to (-1 none) |

## Events

An event is four bytes: its kind and three values.

| Kind | Meaning |
|------|---------|
| 0x00 | a tick: the values count the room's ticks (0 as the room opens) |
| 0x01 | walk to thing `a` |
| 0x02 | use `a` on `b` (`a` a person of the crew or an item) |
| 0x03 | get `a` |
| 0x04 | look at `a` |
| 0x05 | talk to `a` |
| 0x06 | Kirk is at way out `a` |
| 0x07, 0x08, 0x0E | Kirk steps into, stays in, leaves touch area `a` |
| 0x09 | timer `a` has run out (the eight timers are at `DS:C202`) |
| 0x0A | person done: the code given when the walk or animation began |
| 0x0B | the mission begins |
| 0x0C | a saved game was loaded here |

Each list of handlers is a chain of entries: four bytes to match, a word to
the next entry, then code. 0xFF in the second or third byte matches any
value, except for ticks, timers, the start and "done", which must match
exactly. Up to seven tables of lists are stacked at `DS:526A` (kind, table,
priority), the room's own with the mission's and `DATA.RDF`'s. An event goes
to list 0, then list 1, then (for what the player does) lists 2 and 3,
until a handler takes it.

What nobody takes: Kirk walks where the player pointed, and a way out leads
to the room in the table above.

## Coming in

Opening a room queues a tick (0) and brings the crew in: each walks from
their way in to their place, or beams in when the mission begins. Kirk's
arrival gives "done" with code 0xFF, and until then (`DS:C217`) the player
gives no orders.

Some rooms start a scene at once, with the crew still walking in, and give
Kirk an animation of their own. That would swallow his arrival, and the
player could never give an order again. Helmsman lets the arrival through at
once when a script takes Kirk over before he is there.

## Orders

The player gives an order by pointing at a thing: walk, look, talk, get, or
use (first what, then on what). The action figure (right button) picks the
order; the inventory shows what the team carries, `DS:0F5A` (ten bytes an
item: whether it is carried, its name).

## Room functions

Code calls the game with a function number and arguments. "Person" is a
place in the list of 32 people at `DS:73D8` (0 Kirk); -1 asks for a free
place. A "code" of 0xFE means "wait until done" instead.

| Function | What it does |
|----------|--------------|
| 0x00 `person` | the person stands (one of the crew) or leaves |
| 0x01 `person, name, x, y, code` | an animation |
| 0x02 `person, letter` | the letter the person's pictures start with |
| 0x03 `person, ...` | where and in which colours the person's lines show, and their name |
| 0x04 `line` | a line or a choice; gives the choice |
| 0x05 `item` | the team gets the item |
| 0x06 `room, way in` | to another room, unless one of the team is held (team flags 2 and 4) |
| 0x07 `item` | the team loses the item |
| 0x08 `person, x, y, code` | the person walks there |
| 0x09 `name` | another walking map |
| 0x0B, 0x0C | a line drawn across the room (a beam), and away with it |
| 0x0D `item` | whether the team has the item |
| 0x0E `thing` | where a person or a thing is |
| 0x0F `a, b` | a random number from `a` to `b` |
| 0x10 `effect, volume, pan` | a sound effect (see [sequel-sound.md](sequel-sound.md)) |
| 0x11 `now, background` | music |
| 0x12 `a, b` | the team beams up; `a`, `b` are kept at `DS:C222`, `DS:C224` |
| 0x13 `a, b` | the mission is over, the same two values kept |
| 0x14 | the mission is lost: the game-over menu |
| 0x15 `words` | a typed answer: which of the words (from 1), 0 none |
| 0x17 `person, to` | which way the person faces: towards someone, or 0xFA–0xFD |
| 0x18 `kind, a, b, quiet` | an event, answered at once |
| 0x19 `table, priority`, 0x1A `table` | more handlers from the running file, and away again |
| 0x1B `menu, x, y`, 0x1C | a panel of buttons, and away with it |
| 0x20 `lo, hi` | the button clicked within that many ticks (-1 none) |
| 0x21 | the library computer (see [sequel-computer.md](sequel-computer.md)) |
| 0x22 `person, ticks` | the person is held where they stand (team flag 2) |
| 0x23 `person` | the person is gone from the room |
| 0x24–0x26 `person` | about holding and the team: let go, whether held, whether off the team |
| 0x27, 0x28 `person` | on the team, off it |
| 0x29 `person` | which way the person faces, as 0xFA–0xFD |
| 0x2A `person, set, clear` | the person's team flags: those in `set` are set, those in `clear` cleared, those in both change |
| 0x2D `person, kind, code` | the person is stunned, falls, or gets up |
| 0x2E `ticks` | wait |
| 0x2F `table, count` | the player points at one of the things in the table; gives which |
| 0x30 `name` | a scene (see [sequel-scenes.md](sequel-scenes.md)) |
| 0x31–0x33 | a new room picture, new depths, show them now |
| 0x35, 0x36 `ticks`, 0x37 `r, g, b` | fade in; fade out and wait; the colour fades go to |
| 0x38 `name, code` | back to an episode's checkpoint |
| 0x39 `person, animation` | who says the next line, and how they move while talking |
| 0x3A `name`, 0x3B | a sound, and silence |

## Trying a mission out

`helmsman-dump GAME play GAME 0 0 none.scr OUT` with
`HELMSMAN_START="explore EPISODE ROOM WAYIN MOST [ORDERS]"` lets the mission
play itself: from where it starts it tries every order the rooms answer,
keeps each new state (the scripts' values, what is carried, the room) and
goes on from the newest, until the mission is over or MOST states were
found. It prints the orders that led to the end, to a lost game, to beaming
up early or to a script that never lets go. ORDERS, as printed, are given
first; `pick:N` there answers the next choice, button or typed word with
answer N. With `HELMSMAN_TRACE` it also prints every state and the values
that tell it apart.
