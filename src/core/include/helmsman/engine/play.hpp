// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays a game from the player's copy, until it ends or the player leaves
// it for Helmsman's choice of games.
#pragma once

#include "helmsman/data/game_source.hpp"
#include "helmsman/engine/platform.hpp"

namespace helmsman {

void playGame(Platform& platform, GameSource& source);

} // namespace helmsman
