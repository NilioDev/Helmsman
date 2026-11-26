// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saved games: Helmsman's own file format, one file a game, in the player's
// save folder. A file starts with "HLMSAVE", a format number, which game it
// is, a description to show in the list, and then what the game saved. See
// docs/formats/savegame.md.
#pragma once

#include "helmsman/data/reader.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

inline constexpr int kMaxSaves = 20;

class SaveWriter {
public:
    void u8(std::uint8_t v) { data.push_back(v); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void s32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void text(std::string_view s) {
        u16(static_cast<std::uint16_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    }
    void bytes(std::span<const std::uint8_t> b) {
        u32(static_cast<std::uint32_t>(b.size()));
        data.insert(data.end(), b.begin(), b.end());
    }
    std::vector<std::uint8_t> data;
};

// Reading back what a SaveWriter wrote; every read is bounds checked.
class SaveReader : public Reader {
public:
    using Reader::Reader;
    std::int32_t s32() { return static_cast<std::int32_t>(u32()); }
    std::string text() {
        const auto n = u16();
        const auto b = bytes(n);
        return std::string(b.begin(), b.end());
    }
    std::vector<std::uint8_t> blob(std::size_t max) {
        const auto n = u32();
        if (n > max) {
            throw FormatError("a saved part is too large");
        }
        const auto b = bytes(n);
        return std::vector<std::uint8_t>(b.begin(), b.end());
    }
};

struct SaveSlot {
    int number = 0;
    std::string description;
};

// The saved games of `game` ("anniversary", "judgment") in `folder`, newest
// first.
std::vector<SaveSlot> listSaves(const std::filesystem::path& folder, std::string_view game);
// Writes a save; false when it could not be written.
bool writeSave(const std::filesystem::path& folder, std::string_view game, int number, std::string_view description,
               std::span<const std::uint8_t> payload);
// The saved data, or nothing when the file is missing or not a save of `game`.
std::optional<std::vector<std::uint8_t>> readSave(const std::filesystem::path& folder, std::string_view game, int number);
// The lowest unused number.
int freeSaveNumber(const std::filesystem::path& folder, std::string_view game);

} // namespace helmsman
