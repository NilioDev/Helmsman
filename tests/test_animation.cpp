// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/data/animation.hpp"
#include "helmsman/data/reader.hpp"

#include <cstring>

using namespace helmsman;

namespace {

void put16(std::vector<std::uint8_t>& to, std::size_t at, int v) {
    to[at] = static_cast<std::uint8_t>(v);
    to[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// A 4x2 animation with the given records, all in one page.
std::vector<std::uint8_t> build(const std::vector<std::vector<std::uint8_t>>& records, bool loopRecord = false) {
    std::vector<std::uint8_t> f(0xB00, 0);
    std::memcpy(f.data(), "LPF ", 4);
    std::memcpy(f.data() + 16, "ANIM", 4);
    put16(f, 4, 256);
    put16(f, 6, 1);
    put16(f, 8, static_cast<int>(records.size()));
    put16(f, 20, 4);
    put16(f, 22, 2);
    put16(f, 64, static_cast<int>(records.size()));
    f[26] = loopRecord ? 1 : 0;
    put16(f, 68, 10);
    f[0x100 + 4 * 3 + 2] = 0xFC; // colour 3: red
    put16(f, 0x500, 0);
    put16(f, 0x502, static_cast<int>(records.size()));
    std::vector<std::uint8_t> page(8 + records.size() * 2, 0);
    put16(page, 2, static_cast<int>(records.size()));
    for (std::size_t i = 0; i < records.size(); ++i) {
        std::vector<std::uint8_t> rec = {'B', 0, 1, 0};
        rec.insert(rec.end(), records[i].begin(), records[i].end());
        if (records[i].empty()) {
            rec.clear();
        }
        put16(page, 8 + i * 2, static_cast<int>(rec.size()));
        page.insert(page.end(), rec.begin(), rec.end());
    }
    f.insert(f.end(), page.begin(), page.end());
    return f;
}

} // namespace

HLM_TEST("animation: runs, skips and copies over the frame before") {
    const auto file = build({
        {0x00, 8, 3, 0x80, 0, 0},           // eight pixels of colour 3
        {0x81, 0x02, 5, 6, 0x80, 0, 0},     // skip one, copy two
        {},                                 // the same again
        {0x80, 0x06, 0x00, 0x80, 0x01, 0xC0, 9, 0x80, 0, 0}, // long skip six, long run of one 9
        {0x80, 0x02, 0x80, 7, 8, 0x80, 0, 0}, // long copy of two
    });
    Animation anim(file);
    HLM_CHECK(anim.width() == 4 && anim.height() == 2 && anim.frameCount() == 5);
    HLM_CHECK(anim.framesPerSecond() == 10);
    HLM_CHECK(anim.palette()[3].r == 63);
    anim.next();
    HLM_CHECK(anim.picture() == std::vector<std::uint8_t>(8, 3));
    anim.next();
    HLM_CHECK((anim.picture() == std::vector<std::uint8_t>{3, 5, 6, 3, 3, 3, 3, 3}));
    anim.next();
    HLM_CHECK(anim.frame() == 2 && anim.picture()[1] == 5);
    anim.next();
    HLM_CHECK(anim.picture()[6] == 9 && anim.picture()[7] == 3);
    anim.next();
    HLM_CHECK(anim.picture()[0] == 7 && anim.picture()[1] == 8);
    anim.next(); // round again
    HLM_CHECK(anim.frame() == 0 && anim.picture() == std::vector<std::uint8_t>(8, 3));
}

HLM_TEST("animation: a last record that leads back to the first frame") {
    Animation anim(build({{0x00, 8, 3, 0x80, 0, 0}, {0x01, 4, 0x80, 0, 0}, {0x01, 3, 0x80, 0, 0}}, true));
    HLM_CHECK(anim.frameCount() == 2);
    anim.next();
    anim.next();
    HLM_CHECK(anim.frame() == 1 && anim.picture()[0] == 4);
    anim.next();
    HLM_CHECK(anim.frame() == 0 && anim.picture()[0] == 3 && anim.picture()[1] == 3);
}

HLM_TEST("animation: damaged files are refused") {
    HLM_CHECK_THROWS(Animation(std::vector<std::uint8_t>(100, 0)));
    Animation past(build({{0x00, 9, 3, 0x80, 0, 0}}));
    HLM_CHECK_THROWS(past.next());
    Animation cut(build({{0x05, 1, 2}}));
    HLM_CHECK_THROWS(cut.next());
}

HLM_TEST("animation: every painted animation of the sequel plays") {
    auto game = test::openGame(GameId::JudgmentRites);
    if (!game) {
        HLM_SKIP("the sequel is not in gamedata/");
    }
    for (const char* name : {"LOGO", "FEDOPEN", "WARP1", "BOOMER1", "STADORB"}) {
        auto data = game->read(std::string("UNCOMP/") + name + ".ANM");
        HLM_CHECK(data.has_value());
        Animation anim(std::move(*data));
        for (int i = 0; i < anim.frameCount(); ++i) {
            anim.next();
        }
        HLM_CHECK(anim.frameCount() > 30);
    }
}
