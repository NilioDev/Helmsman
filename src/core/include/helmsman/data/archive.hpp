// SPDX-License-Identifier: GPL-3.0-or-later
//
// The games' packed archives: a directory (NAME.DIR), the packed files
// (NAME.001) and, for runs of files with numbered names, a list of where
// each one starts (NAME.RUN). Both games use the same kind; the sequel has
// one for each mission besides DATA. See docs/formats/archive.md.
#pragma once

#include "helmsman/data/game_source.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

// Unpacks the games' LZSS packing into `size` bytes. Never reads outside
// `packed`: data that ends early leaves the rest 0, as does a copy from
// before the start.
std::vector<std::uint8_t> unpackLzss(std::span<const std::uint8_t> packed, std::size_t size);

class Archive {
public:
    // Reads BASE.DIR and BASE.RUN, and keeps BASE.001 in memory. Throws
    // FormatError when they are missing or damaged.
    static Archive open(GameSource& source, std::string_view base);
    // The same from the three files' bytes.
    static Archive fromBytes(std::vector<std::uint8_t> dir, std::span<const std::uint8_t> run,
                             std::vector<std::uint8_t> packed);

    bool has(std::string_view name) const;
    // Unpacks a file by its name ("BRIDGE.BMP", any case). Empty when there
    // is none; throws FormatError when it is damaged.
    std::optional<std::vector<std::uint8_t>> load(std::string_view name) const;
    // The same, but a missing file throws FormatError.
    std::vector<std::uint8_t> need(std::string_view name) const;

    // Every name, in the order of the files in BASE.001.
    std::vector<std::string> names() const;

private:
    std::map<std::string, std::uint32_t> offsets_; // upper case name -> offset in .001
    std::vector<std::uint8_t> packed_;
};

// The name that follows `name` in a numbered run: its number counts up in
// the digits at the end of the name part, or its last letter when it ends
// in none ("ADMIRA09.BMP" -> "ADMIRA10.BMP").
std::string nextInRun(std::string_view name);

} // namespace helmsman
