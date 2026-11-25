// SPDX-License-Identifier: GPL-3.0-or-later
//
// Music files in the Extended MIDI form of the early 1990s: an IFF file with
// one or more songs, each a list of the instruments it uses and its events.
// See docs/formats/music.md.
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace helmsman::audio {

struct XmidiSong {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> timbres; // (patch, bank) the song uses
    std::vector<std::uint8_t> events;
};

// Reads every song of a music file. Throws FormatError when the file is not
// one; songs that are cut short end where the data ends.
std::vector<XmidiSong> readXmidi(std::span<const std::uint8_t> bytes);

} // namespace helmsman::audio
