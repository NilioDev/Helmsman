# Playing with a gamepad

Helmsman works with any gamepad SDL knows (Xbox, PlayStation, Switch Pro
and most others). The games were made for a mouse, so the gamepad drives
the mouse pointer.

## As Helmsman comes

| Button | Does |
|--------|------|
| Left stick | moves the pointer, faster the further it is pushed |
| Direction pad | the pointer jumps to the nearest thing that can be clicked that way |
| A, X (cross, square) | left click |
| B, Y (circle, triangle) | right click |
| Start | the game's options (as the Esc key) |
| Back | nothing |
| Other buttons | press a key, which skips a video or a pause |

The pointer follows the gamepad from the moment the gamepad moves it or
clicks, and follows the mouse again as soon as the mouse moves.

## The direction pad's jumps

Each screen tells Helmsman what on it can be clicked: the bridge's
stations, the things in a room, the buttons of a menu. Pushing a direction
moves the pointer to the middle of the nearest of these lying that way. A
target counts as nearer the less it lies to the side. When nothing lies
that way, the pointer stays.

## Typing

Where the game asks for a typed answer (the second game's library computer
and a few puzzles), the letters are on the screen as keys: jump to one with
the direction pad and click it. "Back" takes the last letter away and
"Done" answers.

## Your own choices

Put a file named `gamepad.txt` in Helmsman's settings folder:

- Windows: `%APPDATA%\NilioDev\Helmsman\`
- macOS: `~/Library/Application Support/NilioDev/Helmsman/`
- Linux: `~/.local/share/NilioDev/Helmsman/`

Each line is `button = action`. Case, spaces, dashes and underscores do
not matter, and `#` starts a comment. Lines Helmsman does not understand
are left out.

Buttons: `a` (or `south`, `cross`), `b` (`east`, `circle`), `x` (`west`,
`square`), `y` (`north`, `triangle`), `back` (`select`), `guide`,
`start`, `leftstick`, `rightstick`, `leftshoulder` (`lb`),
`rightshoulder` (`rb`), `dpup` (`up`), `dpdown` (`down`), `dpleft`
(`left`), `dpright` (`right`).

Actions: `none`, `click`, `right click`, `key` (or `skip`), `jump up`,
`jump down`, `jump left`, `jump right`, `menu` (or `options`), `quit`.

For example:

```
# Swap the shoulder buttons' jobs
lb = right click
rb = click
back = quit
```
