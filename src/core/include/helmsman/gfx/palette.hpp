// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 256-colour VGA palette, kept as the games store it: 6 bits per
// channel (0..63). See docs/formats/pictures.md.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace helmsman {

struct Colour {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    bool operator==(const Colour&) const = default;
};

class Palette {
public:
    Colour& operator[](std::size_t i) { return colours_[i & 0xFF]; }
    const Colour& operator[](std::size_t i) const { return colours_[i & 0xFF]; }

    // A .PAL file: 256 colours of three bytes. Throws FormatError when it
    // is shorter.
    static Palette fromBytes(std::span<const std::uint8_t> bytes);

    // Every colour scaled towards black: `level` 0 is black, 256 unchanged.
    Palette faded(int level) const;
    // Every colour moved towards `target`: `percent` 0 is all target, 100
    // unchanged.
    Palette towards(Colour target, int percent) const;

    // A colour scaled to 8 bits per channel, the way a VGA card shows it.
    static std::uint8_t to8Bit(std::uint8_t sixBit) {
        const unsigned v = sixBit & 0x3Fu;
        return static_cast<std::uint8_t>((v << 2) | (v >> 4));
    }

    bool operator==(const Palette&) const = default;

private:
    std::array<Colour, 256> colours_{};
};

} // namespace helmsman
