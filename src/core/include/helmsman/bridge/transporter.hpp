// SPDX-License-Identifier: GPL-3.0-or-later
//
// The transporter room, as the landing party beams down to a planet or back
// up to the ship.
#pragma once

#include "helmsman/away/session.hpp"

#include <string>

namespace helmsman::bridge {

// Plays the beaming: `down` to the planet, or back. `crew` is how many of
// the landing party beam (3 when the security officer was lost); `extra`
// is another animation to play with them (a guest), or empty.
void transporter(away::Session& session, bool down, int crew, const std::string& extra = {});

} // namespace helmsman::bridge
