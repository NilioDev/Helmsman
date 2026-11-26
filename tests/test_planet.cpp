// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/data/program.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/space/planet.hpp"

#include <algorithm>
#include <set>

using namespace helmsman;

namespace {

void put16(std::vector<std::uint8_t>& b, std::size_t at, int v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

// A tiny packed program: 16 bytes of data that unpack to themselves twice
// followed by sixteen 'Z's.
std::vector<std::uint8_t> packedProgram() {
    std::vector<std::uint8_t> exe(32 + 32 + 16, 0);
    put16(exe, 0, 0x5A4D);
    put16(exe, 2, static_cast<int>(exe.size()));
    put16(exe, 4, 1);
    put16(exe, 8, 2);     // header: two paragraphs
    put16(exe, 0x16, 2);  // starts at segment 2 of the image
    std::uint8_t* image = exe.data() + 32;
    for (int i = 0; i < 16; ++i) {
        image[i] = static_cast<std::uint8_t>('a' + i);
    }
    const std::uint8_t commands[] = {0x10, 0x00, 0xB3, 'Z', 0x10, 0x00, 0xB0};
    std::copy(std::begin(commands), std::end(commands), image + 16);
    std::fill(image + 23, image + 32, 0xFF);
    put16(exe, 32 + 32 + 12, 3);     // three paragraphs unpacked
    put16(exe, 32 + 32 + 14, 0x4252); // "RB"
    return exe;
}

} // namespace

HLM_TEST("program: packed image unpacks") {
    const auto image = programImage(packedProgram());
    HLM_CHECK(image.size() == 48);
    for (int i = 0; i < 16; ++i) {
        HLM_CHECK(image[static_cast<std::size_t>(i)] == 'a' + i);
        HLM_CHECK(image[static_cast<std::size_t>(16 + i)] == 'a' + i);
        HLM_CHECK(image[static_cast<std::size_t>(32 + i)] == 'Z');
    }
}

HLM_TEST("program: damaged packing refused") {
    auto exe = packedProgram();
    exe[32 + 18] = 0xC7; // not a command
    HLM_CHECK_THROWS(programImage(exe));
    auto big = packedProgram();
    big[32 + 16] = 0xFF; // copies more than there is
    big[32 + 17] = 0x7F;
    HLM_CHECK_THROWS(programImage(big));
    HLM_CHECK_THROWS(programImage(std::vector<std::uint8_t>{'M', 'Z', 0}));
    const std::vector<std::uint8_t> plain{'X', 'Y'};
    HLM_CHECK_THROWS(programImage(plain));
}

HLM_TEST("planets: surfaces come out the same every time") {
    space::PlanetLook look;
    for (auto& ramp : look.ramps) {
        for (std::size_t i = 0; i < ramp.size(); ++i) {
            ramp[i] = static_cast<std::uint8_t>(100 + i);
        }
    }
    const auto a = space::planetSurface(look, 0x1C5, 6);
    const auto b = space::planetSurface(look, 0x1C5, 6);
    const auto c = space::planetSurface(look, 0x1C6, 6);
    HLM_CHECK(a.size() == 128u * 128u);
    HLM_CHECK(a == b);
    HLM_CHECK(a != c);
    // Every shade is used somewhere between the lowest and highest point.
    const std::set<std::uint8_t> used(a.begin(), a.end());
    HLM_CHECK(used.count(100) == 1);
    HLM_CHECK(*used.rbegin() <= 124);
}

HLM_TEST("planets: a too short table is refused") {
    std::vector<std::uint8_t> table(100);
    std::vector<std::uint8_t> program(0x40000, 1);
    HLM_CHECK_THROWS(space::PlanetLook::fromBytes(table, program));
}

HLM_TEST("planets: drawn round from the game's table") {
    const auto game = test::openGame(GameId::Anniversary);
    if (!game) {
        HLM_SKIP("no game data");
    }
    const auto archive = Archive::open(*game, "DATA");
    const auto program = game->read(programName(GameId::Anniversary));
    HLM_CHECK(program.has_value());
    const auto look = space::PlanetLook::load(archive, programImage(*program));
    HLM_CHECK(look != nullptr);
    const auto surface = space::planetSurface(*look, 0x358, 8);
    for (int size = 0; size < space::PlanetLook::kSizes; ++size) {
        const Bitmap b = space::planetPicture(*look, surface, size, 100, 30);
        const int n = space::kPlanetPixels[static_cast<std::size_t>(size)];
        HLM_CHECK(b.width() == n && b.height() == n);
        // The middle is planet, the corners are not.
        HLM_CHECK(b.pixels.at(n / 2, n / 2) != 0);
        HLM_CHECK(b.pixels.at(0, 0) == 0);
        HLM_CHECK(b.pixels.at(n - 1, n - 1) == 0);
    }
}
