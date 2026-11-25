// SPDX-License-Identifier: GPL-3.0-or-later
//
// The games' boxes and menus over a scene (see ui.cpp).
#pragma once

#include "helmsman/away/scene.hpp"
#include "helmsman/away/session.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace helmsman::away {

struct Ui {
    Session& session;
    const Scene& scene;
    std::function<void()> redraw; // draws the scene under the box on the screen
};

// Sets the mouse pointer to a picture of the game.
void setPointer(Session& session, const std::string& name);

// A text box near (x, y): lines[0] is the speaker (may be empty), the rest
// are choices the player turns through. Voices ("#DIR\FILE#text") are
// played. Gives the choice taken.
int textBox(Ui& ui, const std::vector<std::string>& lines, int x, int y, std::uint8_t colour);

// A row of picture buttons from a menu file (.MNU), placed from (x, y).
// Buttons whose bit (1 << (value + 1)) is in `hidden` are left out. Gives
// the value of the button chosen, or -1.
int buttonMenu(Ui& ui, const std::string& file, int x, int y, std::uint32_t hidden);

} // namespace helmsman::away
