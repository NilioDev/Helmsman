// SPDX-License-Identifier: GPL-3.0-or-later
//
// The planets on the viewscreen. Each has a surface made from its own
// number by splitting lines in half again and again and moving each middle
// point up or down by chance, coloured with one of the game's colour ramps.
// It is drawn as a round picture: a table from the game's data says, for
// each pixel of a disc of a few sizes, which point of the surface lies
// there, so a planet turns by shifting where the table reads. See
// docs/formats/planets.md.
#pragma once

#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace helmsman::space {

// What every planet is drawn with.
struct PlanetLook {
    static constexpr int kSide = 128;   // the surface is kSide x kSide points
    static constexpr int kRamps = 22;
    static constexpr int kShades = 25;
    static constexpr int kSizes = 5;

    std::vector<std::uint8_t> table; // PLANET.DAT
    std::array<std::array<std::uint8_t, kShades>, kRamps> ramps{};

    // Reads PLANET.DAT from the archive and the colour ramps from the
    // game's program as loaded in memory (see programImage). Gives null when
    // either is missing or does not look right.
    static std::shared_ptr<const PlanetLook> load(const Archive& archive, std::span<const std::uint8_t> program);
    // The same from bytes already read; throws FormatError.
    static PlanetLook fromBytes(std::span<const std::uint8_t> table, std::span<const std::uint8_t> program);
};

// The pixels across of each size a planet is drawn at, smallest first, and
// how much each one is enlarged on screen less than the smallest.
constexpr std::array<int, PlanetLook::kSizes> kPlanetPixels{11, 21, 31, 41, 61};
constexpr std::array<int, PlanetLook::kSizes> kPlanetShrink{1, 2, 3, 4, 6};

// A planet's surface, kSide x kSide colours, from its number and ramp.
std::vector<std::uint8_t> planetSurface(const PlanetLook& look, int seed, int ramp);

// The planet seen from a direction: `size` picks one of kPlanetPixels;
// `longitude` and `latitude` are in 1024ths of a turn, latitude from -256
// (seen from straight below) to 256.
Bitmap planetPicture(const PlanetLook& look, std::span<const std::uint8_t> surface, int size, int longitude,
                     int latitude);

} // namespace helmsman::space
