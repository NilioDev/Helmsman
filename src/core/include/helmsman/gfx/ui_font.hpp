// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helmsman's own small font, for its own screens (the game choice and the
// options), drawn for Helmsman: 5 by 7 pixels in cells of 6 by 9, for the
// printable ASCII characters.
#pragma once

#include "helmsman/gfx/surface.hpp"

#include <cstdint>
#include <string_view>

namespace helmsman::ui {

inline constexpr int kCellW = 6;
inline constexpr int kCellH = 9;

int textWidth(std::string_view text);
void drawText(Surface& to, int x, int y, std::string_view text, std::uint8_t colour);
// Centred on x.
void drawCentred(Surface& to, int x, int y, std::string_view text, std::uint8_t colour);

} // namespace helmsman::ui
