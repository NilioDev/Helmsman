// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/gfx/font.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>

namespace helmsman {

Font Font::fromBytes(std::span<const std::uint8_t> bytes) {
    Font f;
    if (bytes.size() < f.glyphs_.size()) {
        throw FormatError("the font is too short");
    }
    std::copy_n(bytes.begin(), f.glyphs_.size(), f.glyphs_.begin());
    return f;
}

void Font::drawChar(Surface& to, int x, int y, char c, std::uint8_t colour) const {
    for (int row = 0; row < kSize; ++row) {
        for (int col = 0; col < kSize; ++col) {
            const std::uint8_t p = pixel(static_cast<unsigned char>(c), col, row);
            if (p != 0) {
                to.set(x + col, y + row, colour != 0 ? colour : p);
            }
        }
    }
}

void Font::drawText(Surface& to, int x, int y, std::string_view text, int advance, std::uint8_t colour) const {
    for (const char c : text) {
        drawChar(to, x, y, c, colour);
        x += advance;
    }
}

} // namespace helmsman
