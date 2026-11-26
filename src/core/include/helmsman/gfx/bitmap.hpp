// SPDX-License-Identifier: GPL-3.0-or-later
//
// The games' pictures (.BMP, not the Windows kind): where the picture's
// handle lies inside it, its size, and one palette index a pixel. Colour 0
// is see-through when a picture is drawn over another. See
// docs/formats/pictures.md.
#pragma once

#include "helmsman/gfx/surface.hpp"

#include <cstdint>
#include <span>

namespace helmsman {

struct Bitmap {
    // How far left and up of the drawing position the picture starts.
    int handleX = 0;
    int handleY = 0;
    Surface pixels{0, 0};

    int width() const { return pixels.width(); }
    int height() const { return pixels.height(); }

    // Throws FormatError when the data is too short for its size.
    static Bitmap fromBytes(std::span<const std::uint8_t> bytes);

    // Draws the picture with its handle at (x, y), leaving colour 0 out;
    // `flip` mirrors it left to right.
    void draw(Surface& to, int x, int y, bool flip = false) const;
    // Copies all of it, colour 0 too, with its top left at (x, y).
    void copyTo(Surface& to, int x, int y) const;
};

} // namespace helmsman
