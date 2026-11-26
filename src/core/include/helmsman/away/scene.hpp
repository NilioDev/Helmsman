// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a planet mission shows: the room's backdrop, and pictures (sprites)
// drawn over it in depth order. Each pixel of the room has a depth (its
// .PRI file, 0 to 15); a sprite's pixel shows only where the room is not
// deeper in front of it. See docs/formats/room.md.
#pragma once

#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/font.hpp"
#include "helmsman/gfx/surface.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace helmsman::away {

enum class SpriteMode : std::uint8_t {
    Normal,     // hidden behind deeper parts of the room
    Hidden,     // not drawn
    Overlay,    // drawn over everything; colour 0 darkens what is behind
    Text,       // a grid of font characters; colour 0 darkens what is behind
};

struct Sprite {
    int x = 0;
    int y = 0;
    int depth = 0;  // 0..15; the room hides the sprite where it is deeper
    int order = 0;  // breaks ties between equal depths
    SpriteMode mode = SpriteMode::Normal;
    std::shared_ptr<const Bitmap> bitmap;
    // For Text: the characters, `columns` wide, and their colour.
    std::vector<std::uint8_t> text;
    int columns = 0;
    std::uint8_t textColour = 0;

    // The rectangle it covers on the screen, or empty.
    bool bounds(int& left, int& top, int& w, int& h) const;
    // True when the pixel at (sx, sy) of the screen is a non-see-through
    // pixel of the sprite.
    bool hit(int sx, int sy) const;
};

class Scene {
public:
    Surface backdrop;
    // One depth (0..15) per pixel of the backdrop.
    std::vector<std::uint8_t> depths = std::vector<std::uint8_t>(Surface::kScreenWidth * Surface::kScreenHeight, 0);
    // What colour 0 of an overlay turns each colour into (the palette's .LUT).
    std::array<std::uint8_t, 256> darken{};
    const Font* font = nullptr;

    // Takes a .PRI file: two depths a byte, left one in the high bits.
    void setDepths(std::span<const std::uint8_t> bytes);
    int depthAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= Surface::kScreenWidth || y >= Surface::kScreenHeight) {
            return 0;
        }
        return depths[static_cast<std::size_t>(y * Surface::kScreenWidth + x)];
    }

    // Draws the backdrop and then `sprites` in depth order.
    void draw(Surface& to, const std::vector<const Sprite*>& sprites) const;
    void drawSprite(Surface& to, const Sprite& s) const;
};

// Sorts sprites into drawing order (deepest last).
void sortForDrawing(std::vector<const Sprite*>& sprites);

} // namespace helmsman::away
