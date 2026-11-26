// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game itself, after its opening titles: the bridge, the
// episodes' scripts, and everything they call on. See
// docs/rules/sequel-scripts.md and docs/rules/sequel-bridge.md.
#pragma once

#include "helmsman/data/game_source.hpp"
#include "helmsman/engine/platform.hpp"
#include "helmsman/sequel/scenes.hpp"

namespace helmsman::sequel {

// Plays a new game until the player quits.
void playGame(Platform& platform, Assets& assets);

} // namespace helmsman::sequel
