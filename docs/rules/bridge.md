# The bridge (first game)

Between planets the player is on the bridge: the officers at their
stations, the viewscreen in the middle, and space outside.

## Events

Everything on the bridge is an event: a tick of the clock, an order from
a click or a key, a report from the crew, a closed text box, the landing
party coming back. The mission being played gets each event first and
may answer it itself (its story on the ship: hails, fights, arrivals).
What it leaves goes to the bridge, which does what it always does.

## Orders

Clicking an officer opens their menu; most orders also have a key.

| Officer | Orders |
|---------|--------|
| Kirk | the captain's log, the transporter (beam down when in orbit), options (save, load, sound, text, quit) |
| Spock | ask his advice, the library computer (type a subject) |
| Scott | the repair menu (which system first, and the target analysis), the damage report |
| Uhura | hail (open a channel to whoever is near) |
| Sulu | standard orbit (enter or leave), shields up or down |
| Chekov | the star map, arm or disarm the weapons |

The viewscreen can be zoomed in and out, and the pointer can steer the
ship while the viewscreen is in control mode. Phasers and torpedoes fire
at the point clicked in the viewscreen when the weapons are armed.

## Travel

The star map shows the systems. Picking one warps the ship there: the
Enterprise leaves at warp speed, seen from outside, and arrives facing the
planet of that system (see `formats/planets.md`). A system the mission has
nothing for stays empty; some missions meet a drifting ship (the station,
a derelict, a transport) that comes into view and takes up orbit beside
the Enterprise.

Entering orbit is shown from outside: the ship sweeps in towards the
planet. Leaving orbit turns the ship away before it flies on. Only in
orbit can the landing party beam down.

## Missions

Each mission starts with its title flying by and Kirk's words. Starfleet
reviews it at the end with a score: points got out of the best possible.
The last mission ends with the Admiral's review of the whole year (the
year's share of the best points), followed by the closing titles and the
choice to load a game, start again or quit.

## Damage and repair

Hits in space weaken the shields on the side that was hit; when a shield
is down, the parts behind it take damage. Scott repairs the parts over
time, the one the player chose first before the others, and reports each
part that works again. If the hull is destroyed, the ship is lost: seen
from outside as it breaks apart, then the choice to load, start again or
quit.

Saved games made on the bridge keep the voyage and the ships around it
(see `formats/savegame.md`).
