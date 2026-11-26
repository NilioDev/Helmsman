// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first game's opening titles: the publisher's logo, then the
// Enterprise passing a red planet while the theme plays, the opening
// words, the game's title and the names of the people who made it.
#pragma once

#include "helmsman/away/session.hpp"

namespace helmsman {

// Plays the opening titles; a key or a click skips them.
void playOpeningTitles(away::Session& session);

} // namespace helmsman
