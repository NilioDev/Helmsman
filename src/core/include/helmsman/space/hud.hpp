// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bridge's displays of a fight, as both games draw them: the bars of
// the weapons, the engines' power and the speed above the viewscreen, the
// pictures of a ship's parts and shields at the sides, and the small radar
// on the helm. The games differ only in their colours and in how a picture
// is found. See docs/rules/space.md.
#pragma once

#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/surface.hpp"
#include "helmsman/space/space.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace helmsman::space {

struct HudColours {
    std::uint8_t empty = 0x81;       // the unlit part of a bar
    std::array<std::uint8_t, 2> weapon{0xB1, 0xB2}; // a weapon's bar, port and starboard
    std::uint8_t power = 0xB1;       // the engines' power
    std::uint8_t boost = 0x69;       // the light of extra power
    std::uint8_t speedEmpty = 0;     // the unlit part of the speed bars
    std::uint8_t wantSpeed = 0x69;   // the speed asked for
    std::uint8_t madeSpeed = 0xBA;   // the speed made
    std::uint8_t radarCentre = 0x8B; // the player's ship on the radar
    std::array<std::uint8_t, 4> radarShips{46, 120, 100, 86};
};

using PictureSource = std::function<std::shared_ptr<const Bitmap>(const std::string&)>;

// The weapons (a bar that fills while each comes ready, and a light from
// `lights`, which may be null), the engines' power and the speed.
void drawHudBars(Surface& to, const Space& space, const Bitmap* lights, const HudColours& colours);

// The pictures of `ship`'s parts and shields at the sides of the bridge.
// The panel pictures carry their place on the screen as a negative handle.
// `names` are the picture sets' short names ("ent", "klg", ...).
void drawShipPanels(Surface& to, const Object& ship, const PictureSource& pictures,
                    const std::vector<std::string>& names);

// The radar: 17 by 17 pixels, the player's ship in the middle and the
// others where they lie (the further behind, the further out). `marked`
// is a ship drawn with a cross around it, or -1.
void drawRadar(Surface& radar, const Space& space, const HudColours& colours, int marked = -1);

} // namespace helmsman::space
