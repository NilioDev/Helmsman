// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_data.hpp"
#include "test.hpp"

#include "helmsman/data/reader.hpp"
#include "helmsman/data/sound_bank.hpp"

#include <cmath>
#include <cstring>

using namespace helmsman;

namespace {

std::vector<std::uint8_t> packed(std::uint32_t samples, std::initializer_list<std::uint8_t> stream) {
    std::vector<std::uint8_t> out(26, 0);
    std::memcpy(out.data(), "Interplay ACMP Data\x1A", 20);
    out[20] = 0x11; // 11025 Hz
    out[21] = 0x2B;
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(samples >> (8 * i)));
    }
    out.insert(out.end(), stream);
    return out;
}

} // namespace

HLM_TEST("packed sound: one value, plain samples") {
    auto s = unpackAcmp(packed(3, {0x00, 0x55}));
    HLM_CHECK(s.rate == 11025);
    HLM_CHECK((s.samples == std::vector<std::uint8_t>{0x55, 0x55, 0x55}));
    // Plain 7-bit samples, shifted up by one: fields 5, 10, 64.
    s = unpackAcmp(packed(3, {0x26, 0x05, 0x05, 0x10}));
    HLM_CHECK((s.samples == std::vector<std::uint8_t>{10, 20, 128}));
}

HLM_TEST("packed sound: steps with run lengths") {
    // Runs of length 1 and 2 (bits 0; 0, 1), fields 4 and 3: steps +2, -5.
    const auto s = unpackAcmp(packed(2, {0x03, 0x04, 28}));
    HLM_CHECK((s.samples == std::vector<std::uint8_t>{130, 125}));
}

HLM_TEST("packed sound: halved blocks are filled in") {
    const auto s = unpackAcmp(packed(4, {0x80, 100}));
    HLM_CHECK((s.samples == std::vector<std::uint8_t>{114, 100, 100, 100}));
}

HLM_TEST("packed sound: damaged data is refused or stays in bounds") {
    HLM_CHECK_THROWS(unpackAcmp(std::vector<std::uint8_t>(10, 0)));
    HLM_CHECK_THROWS(unpackAcmp(packed(0xFFFFFFFF, {})));
    HLM_CHECK_THROWS(unpackAcmp(packed(1, {0x1F})));
    // Data that ends early reads as zeros: plain samples go on as silence,
    // a run that never ends is refused.
    HLM_CHECK(unpackAcmp(packed(600, {0x06})).samples.size() == 600);
    HLM_CHECK_THROWS(unpackAcmp(packed(600, {0x01})));
}

HLM_TEST("sound bank: a spoken line matches the CD's unpacked copy") {
    auto game = test::openGame(GameId::JudgmentRites);
    if (!game) {
        HLM_SKIP("the sequel is not in gamedata/");
    }
    SoundBank bank(*game, "LIGHT.VCC");
    HLM_CHECK(bank.names().size() > 800);
    const auto line = bank.sound("l3-1");
    HLM_CHECK(line.has_value());
    const auto plain = game->read("L3-1.VOC");
    HLM_CHECK(plain.has_value());
    const auto reference = decodeBankSound(*plain);
    HLM_CHECK(line->rate == reference.rate);
    const std::size_t n = std::min(line->samples.size(), reference.samples.size());
    HLM_CHECK(n > 100000);
    double diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff += std::abs(int{line->samples[i]} - int{reference.samples[i]});
    }
    // The packing loses a little: on average less than one step.
    HLM_CHECK(diff / static_cast<double>(n) < 1.0);
    for (const char* name : {"DATA.VCC", "BACKSND.VCC"}) {
        SoundBank all(*game, name);
        for (const auto& sound : all.names()) {
            HLM_CHECK(all.sound(sound).has_value());
        }
    }
}
