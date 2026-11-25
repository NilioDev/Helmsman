// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helmsman's own screen at start: the player picks which of the two games
// to play, with the mouse, the keys or a gamepad. A game the player has not
// added yet says how to add it. See docs/rules/shell.md.
#pragma once

#include "helmsman/data/game_source.hpp"
#include "helmsman/engine/platform.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helmsman {

struct GameEntry {
    GameId game = GameId::Anniversary;
    // Where the player's copy is, as shown; empty when it is not found.
    std::string where;
};

struct Library {
    // The games found now; asked again each frame, as the player may add
    // one by dropping its CD image on the window.
    std::function<std::vector<GameEntry>()> games;
    // Opens a found game. Null when it cannot be opened now.
    std::function<std::unique_ptr<GameSource>(GameId)> open;
    // Plays an opened game until it ends or the player leaves it.
    std::function<void(GameSource&)> play;
    // Shown under the choices: what went wrong last, or empty.
    std::string note;
};

// Shows the choice until the player picks a found game or quits. Returns
// the game picked, or nothing to quit.
std::optional<GameId> chooseGame(Platform& platform, Library& library);

// The whole program: the choice, then the game picked, then the choice
// again when the game ends, until the player quits.
void runShell(Platform& platform, Library& library);

// The names shown for each game.
const char* gameTitle(GameId game);
const char* gameYear(GameId game);

} // namespace helmsman
