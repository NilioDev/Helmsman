// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menus the bridge and the planets share: saving, loading, and the
// end of the game when the crew or the ship is lost.
#pragma once

#include "helmsman/away/ui.hpp"

#include <span>
#include <string>

namespace helmsman::away {

// Asks where to save (a new game or one to replace) and writes `payload`
// there, described by the mission's name ("DEMON") and the time. Tells the
// player when it could not be written.
void saveGameMenu(Ui& ui, const std::string& mission, std::span<const std::uint8_t> payload);
// Asks which saved game to load; gives its number, or -1.
int loadGameMenu(Ui& ui);

enum class GameOverChoice {
    Load,
    Restart,
    Quit,
};
// "Game Over": load a saved game, start again, or quit. Asks again when
// the player loads nothing. `load` is the saved game picked.
GameOverChoice gameOverMenu(Ui& ui, int& load);

} // namespace helmsman::away
