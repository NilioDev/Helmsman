# The choice of games

When Helmsman starts, it shows its own screen: a title, a slowly drifting
field of stars, a card for each of the two games and a Quit button below.

## The cards

A card shows the game's name and year. When Helmsman has found the
player's copy, it says "Ready to play", shows where the copy is (cut at
the front with "..." when the path is long) and has a Start button. When
it has not, it says how to add it: drop the CD image (`.cue`, `.iso`) or
the CD's folder on the window.

Helmsman looks for copies in its own `gamedata` folders and in the places
the player dropped before (kept in `games.txt` in the settings folder). It
looks again every frame, so a dropped copy shows at once.

## Choosing

- Mouse or gamepad pointer: pointing at a card or Quit gives it the
  focus; a click picks it.
- Keys: Left and Right move between the cards, Down goes to Quit, Up back
  to the cards, Tab goes round all three. Enter or Space picks. `1` and
  `2` pick a game at once. Esc quits.

Picking a game that is not found does nothing. When a found game cannot be
opened, the screen comes back with a line saying so. When a game ends, or
the player leaves it, the choice comes back.
