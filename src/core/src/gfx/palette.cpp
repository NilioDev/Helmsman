// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/gfx/palette.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>

namespace helmsman {

Palette Palette::fromBytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 768) {
        throw FormatError("a palette is too short");
    }
    Palette p;
    for (std::size_t i = 0; i < 256; ++i) {
        p.colours_[i] = Colour{static_cast<std::uint8_t>(bytes[i * 3] & 0x3F),
                               static_cast<std::uint8_t>(bytes[i * 3 + 1] & 0x3F),
                               static_cast<std::uint8_t>(bytes[i * 3 + 2] & 0x3F)};
    }
    return p;
}

Palette Palette::faded(int level) const {
    level = std::clamp(level, 0, 256);
    Palette p;
    for (std::size_t i = 0; i < 256; ++i) {
        const auto& c = colours_[i];
        p.colours_[i] = Colour{static_cast<std::uint8_t>(c.r * level / 256), static_cast<std::uint8_t>(c.g * level / 256),
                               static_cast<std::uint8_t>(c.b * level / 256)};
    }
    return p;
}

} // namespace helmsman
