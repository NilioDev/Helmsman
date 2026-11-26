// SPDX-License-Identifier: GPL-3.0-or-later
//
// The games' 8 by 8 font (FONT.FNT): 128 characters of 64 bytes, one
// palette index a pixel, 0 left out. See docs/formats/pictures.md.
#pragma once

#include "helmsman/gfx/surface.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace helmsman {

class Font {
public:
    static constexpr int kSize = 8;

    // Throws FormatError when the data is too short.
    static Font fromBytes(std::span<const std::uint8_t> bytes);

    // Draws one character with its top left at (x, y), each colour the
    // font gives replaced by `colour` unless it is 0 there.
    void drawChar(Surface& to, int x, int y, char c, std::uint8_t colour = 0) const;
    // A row of characters, `advance` pixels apart.
    void drawText(Surface& to, int x, int y, std::string_view text, int advance = kSize, std::uint8_t colour = 0) const;

    std::uint8_t pixel(int c, int x, int y) const {
        return glyphs_[static_cast<std::size_t>(c & 0x7F) * 64 + static_cast<std::size_t>(y * 8 + x)];
    }

private:
    std::array<std::uint8_t, 128 * 64> glyphs_{};
};

} // namespace helmsman
