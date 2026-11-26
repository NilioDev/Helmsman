// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's sound banks (.VCC): many short sounds and spoken lines
// in one file, most of them packed with a small lossy coding. See
// docs/formats/sound-bank.md.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

class GameSource;

// A sound as 8-bit unsigned mono samples.
struct PlainSound {
    int rate = 0;
    std::vector<std::uint8_t> samples;
};

class SoundBank {
public:
    // Reads the bank's list of sounds. Throws FormatError.
    SoundBank(GameSource& source, std::string path);

    bool has(std::string_view name) const;
    std::vector<std::string> names() const;
    // The sound's stored bytes, or nothing when the bank has no such sound.
    std::optional<std::vector<std::uint8_t>> raw(std::string_view name) const;
    // The sound ready to play. Throws FormatError when it is damaged.
    std::optional<PlainSound> sound(std::string_view name) const;

private:
    struct Entry {
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };
    GameSource* source_;
    std::string path_;
    std::map<std::string, Entry, std::less<>> entries_;
};

// Unpacks a sound that starts with "Interplay ACMP Data". Throws FormatError.
PlainSound unpackAcmp(std::span<const std::uint8_t> bytes);

// Any sound the bank holds, packed or a VOC file. Throws FormatError.
PlainSound decodeBankSound(std::span<const std::uint8_t> bytes);

} // namespace helmsman
