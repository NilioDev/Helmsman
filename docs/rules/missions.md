# Planet missions (away teams)

On a planet the player leads the landing party: Kirk, Spock, McCoy and a
security officer. Each room is a picture with its own code (see
`formats/room.md`), which decides what happens in it.

## Actions

The right mouse button (or the gamepad's right click) opens the action
menu: walk, use, get, look, talk and options. The pointer changes to show
the action chosen, and lights up over things the action can be used on.

- **Walk**: Kirk walks to the point clicked; the others follow.
- **Use**: an item from the inventory, or a crew member's skill (Spock's
  tricorder, McCoy's medical kit), on something in the room.
- **Get**, **Look**, **Talk**: on something or someone in the room.
- **Options**: save, load, sound, text, quit.

Each action becomes an event (`formats/room.md`): the room's code answers
it, or the game gives its usual answer ("Nothing happens.").

When something has a place to walk to first, Kirk walks there and the
action happens when he arrives.

## Crew and rooms

The crew walks on the room's walk map and is drawn larger the lower on
the screen they stand. Walking onto a way out takes the crew to the next
room, coming in through the matching way in. Some places set something
off when Kirk walks onto them.

The room's timers count down every tick and tell the room's code when
they run out. Conversations show a text box with the speaker's name; when
there is a choice, the player turns through the lines with the arrows and
picks one. With voices on, the spoken line plays.

## Ending

The mission ends when the crew beams up, with a score of points got out
of the best possible. If the landing party is lost, the game offers to
load a saved game, start again or quit.
