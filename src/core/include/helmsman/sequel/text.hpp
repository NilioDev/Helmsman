// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's writing: its font (FONT.FON), and the boxes it puts
// spoken lines and choices in. See docs/formats/sequel-font.md.
#pragma once

#include "helmsman/gfx/palette.hpp"
#include "helmsman/gfx/surface.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman::sequel {

class SequelFont {
public:
    static constexpr int kWidth = 8;
    static constexpr int kHeight = 11;
    static constexpr int kColours = 10; // colour sets a line can be written in

    // Throws FormatError when the data is too short.
    static SequelFont fromBytes(std::span<const std::uint8_t> bytes);

    // The colour sets come from the game's program; without them a plain
    // set is used.
    void setColours(std::span<const std::uint8_t> table);

    // Draws a character with its top left at (x, y) in colour set `set`.
    // Where the character is empty, `shade` (when given) darkens what is
    // there.
    void drawChar(Surface& to, int x, int y, std::uint8_t c, int set, const std::uint8_t* shade = nullptr) const;
    void drawText(Surface& to, int x, int y, std::string_view text, int set, const std::uint8_t* shade = nullptr) const;

    // The 3-bit value of a pixel of a character.
    int pixel(std::uint8_t c, int x, int y) const;

private:
    std::vector<std::uint8_t> glyphs_;
    std::array<std::uint8_t, kColours * 8> colours_{};
};

// For each colour, the palette colour nearest to it at `percent` of its
// brightness: what a see-through dark box does to what is behind it.
std::array<std::uint8_t, 256> shadeTable(const Palette& palette, int percent);

// Breaks a line into rows of at most `columns` characters, at spaces.
std::vector<std::string> wrapText(std::string_view text, std::size_t columns);

// Takes the "#tag#" a spoken line starts with off it: the name of its
// recorded sound. Returns the tag (empty when there is none).
std::string takeSoundTag(std::string& text);

} // namespace helmsman::sequel
