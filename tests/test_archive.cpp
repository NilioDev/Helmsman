// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/data/archive.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"

using namespace helmsman;

HLM_TEST("lzss: literals and a copy back") {
    // Flags 0b11110111: three literals, a copy, four literals.
    const std::vector<std::uint8_t> packed = {0xF7, 'a', 'b', 'c', 0x30, 0x00, 'x', 'y', 'z', 'w'};
    // The copy: 3 back, 0 + 3 long -> "abc".
    const auto out = unpackLzss(packed, 10);
    HLM_CHECK(std::string(out.begin(), out.end()) == "abcabcxyzw");
}

HLM_TEST("lzss: a copy may overlap what it writes") {
    const std::vector<std::uint8_t> packed = {0x01, 'a', 0x14, 0x00};
    const auto out = unpackLzss(packed, 8);
    HLM_CHECK(std::string(out.begin(), out.end()) == "aaaaaaaa");
}

HLM_TEST("lzss: damaged data stays inside its bounds") {
    HLM_CHECK(unpackLzss(std::vector<std::uint8_t>{0x00, 0x50, 0x00}, 4) == std::vector<std::uint8_t>(4, 0));
    HLM_CHECK(unpackLzss(std::vector<std::uint8_t>{0x01}, 4) == std::vector<std::uint8_t>(4, 0));
    HLM_CHECK(unpackLzss(std::vector<std::uint8_t>{0x01, 'a', 0x00}, 3).size() == 3);
}

HLM_TEST("archive: numbered runs count up") {
    HLM_CHECK(nextInRun("ADMIRA09.BMP") == "ADMIRA10.BMP");
    HLM_CHECK(nextInRun("BLDR1.ANM") == "BLDR2.ANM");
    HLM_CHECK(nextInRun("PRELA.BMP") == "PRELB.BMP");
}

HLM_TEST("archive: a directory with a run") {
    std::vector<std::uint8_t> dir;
    auto entry = [&](const char* name, const char* ext, std::uint32_t value) {
        std::uint8_t e[14] = {};
        for (int i = 0; i < 8 && name[i]; ++i) e[i] = static_cast<std::uint8_t>(name[i]);
        for (int i = 0; i < 3 && ext[i]; ++i) e[8 + i] = static_cast<std::uint8_t>(ext[i]);
        e[11] = static_cast<std::uint8_t>(value);
        e[12] = static_cast<std::uint8_t>(value >> 8);
        e[13] = static_cast<std::uint8_t>(value >> 16);
        dir.insert(dir.end(), e, e + 14);
    };
    // Each packed file: size 1, packed size 2, flags, the byte.
    const std::vector<std::uint8_t> packed = {1, 0, 2, 0, 1, 'A', 1, 0, 2, 0, 1, 'B', 1, 0, 2, 0, 1, 'C'};
    entry("ONE", "TXT", 0);
    entry("RUN1", "TXT", 0x800000 | (2 << 16) | 0); // two files from RUN offset 0
    const std::vector<std::uint8_t> run = {6, 0, 0, 6, 0};
    const auto archive = Archive::fromBytes(dir, run, packed);
    HLM_CHECK(archive.need("one.txt") == std::vector<std::uint8_t>{'A'});
    HLM_CHECK(archive.need("RUN1.TXT") == std::vector<std::uint8_t>{'B'});
    HLM_CHECK(archive.need("RUN2.TXT") == std::vector<std::uint8_t>{'C'});
    HLM_CHECK(!archive.load("RUN3.TXT"));
}

HLM_TEST("archive: every file of the 1992 game unpacks") {
    auto game = test::openGame(GameId::Anniversary);
    if (!game) {
        HLM_SKIP("the 1992 game is not in gamedata/");
    }
    const auto archive = Archive::open(*game, "DATA");
    int count = 0;
    for (const auto& name : archive.names()) {
        archive.need(name);
        ++count;
    }
    HLM_CHECK(count > 7000);
    const auto bridge = Bitmap::fromBytes(archive.need("BRIDGE.BMP"));
    HLM_CHECK(bridge.width() == 320 && bridge.height() == 200);
}

HLM_TEST("archive: every file of the sequel unpacks") {
    auto game = test::openGame(GameId::JudgmentRites);
    if (!game) {
        HLM_SKIP("the sequel is not in gamedata/");
    }
    for (const char* base : {"DATA", "FED", "LIGHT", "MADNESS", "METH", "NOMAN", "SCOTTY", "SENTNL", "VOIDS"}) {
        const auto archive = Archive::open(*game, base);
        for (const auto& name : archive.names()) {
            archive.need(name);
        }
    }
}
