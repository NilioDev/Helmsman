# Space and ship fights

Space is drawn the way the viewscreen sees it: stars streaming past, the
other ships, torpedoes and phaser beams, explosions, and the system's
planet turning slowly.

## Ships

Each kind of ship is described by its `.DSC` file (`formats/ships.md`):
its shields and parts, how fast it can turn and fly, the distance it likes
to fight at, whether it can cloak, and its four weapons.

Every tick:

- A ship's power depends on its warp and impulse parts; with less power
  it flies slower.
- A ship turns towards the turn it is asked for, no faster than its turn
  changes allow, and its speed moves towards the speed asked for.
- The other ships chase the player, try to keep their liked distance, and
  sometimes break off in a set move (a turn away, a loop). A ship told to
  run away turns and flies off.
- A weapon fires when it is ready and its target lies within its reach;
  its shot flies at the weapon's speed for the weapon's life.
- A cloaking ship fades out over 28 steps and cannot fire while cloaked.

## Hits

A shot hits when it comes nearer to a ship than the ship's size. The side
that was hit (front, back, left, right, top, bottom) is worked out from
where the shot was in the ship's own axes. The shield of that side takes
the damage while it lasts; after that a part behind it does. A hit on the
sensors makes the viewscreen show interference until they are repaired.

## The player's controls

The pointer's distance from the middle of the viewscreen sets how fast the
Enterprise turns; the keys set the speed from 0 (stop) to 9 (full), and
there is a full reverse. Phasers and torpedoes fire at the point of the
viewscreen clicked.
