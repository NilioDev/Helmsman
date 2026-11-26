// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/gfx/bitmap.hpp"

#include "helmsman/data/reader.hpp"

#include <cstring>

namespace helmsman {

Bitmap Bitmap::fromBytes(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);
    Bitmap b;
    b.handleX = r.s16();
    b.handleY = r.s16();
    const int w = r.u16();
    const int h = r.u16();
    if (w > 1024 || h > 1024) {
        throw FormatError("a picture is too large");
    }
    const auto pixels = r.bytes(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    b.pixels = Surface(w, h);
    if (!pixels.empty()) {
        std::memcpy(b.pixels.data(), pixels.data(), pixels.size());
    }
    return b;
}

void Bitmap::draw(Surface& to, int x, int y, bool flip) const {
    const int left = x - handleX;
    const int top = y - handleY;
    const int w = width();
    for (int row = 0; row < height(); ++row) {
        const int ty = top + row;
        if (ty < 0 || ty >= to.height()) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const std::uint8_t c = pixels.at(flip ? w - 1 - col : col, row);
            if (c != 0) {
                to.set(left + col, ty, c);
            }
        }
    }
}

void Bitmap::copyTo(Surface& to, int x, int y) const { to.blit(pixels, 0, 0, width(), height(), x, y); }

} // namespace helmsman
