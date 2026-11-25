// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/audio/xmidi.hpp"

#include "helmsman/data/reader.hpp"

#include <cstring>
#include <string_view>

namespace helmsman::audio {

namespace {

struct Chunk {
    std::string_view id;
    std::span<const std::uint8_t> data;
};

std::uint32_t big32(std::span<const std::uint8_t> b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3];
}

// The chunks of an IFF body, one after the other, each padded to even.
std::vector<Chunk> chunks(std::span<const std::uint8_t> body) {
    std::vector<Chunk> out;
    std::size_t p = 0;
    while (p + 8 <= body.size()) {
        const std::string_view id(reinterpret_cast<const char*>(body.data() + p), 4);
        std::size_t len = big32(body, p + 4);
        p += 8;
        if (len > body.size() - p) {
            len = body.size() - p;
        }
        out.push_back(Chunk{id, body.subspan(p, len)});
        p += len + (len & 1);
    }
    return out;
}

void readSong(std::span<const std::uint8_t> form, std::vector<XmidiSong>& songs) {
    // form: "XMID" then chunks.
    if (form.size() < 4 || std::memcmp(form.data(), "XMID", 4) != 0) {
        return;
    }
    XmidiSong song;
    for (const auto& c : chunks(form.subspan(4))) {
        if (c.id == "TIMB" && c.data.size() >= 2) {
            const std::size_t count = c.data[0] | (c.data[1] << 8);
            for (std::size_t i = 0; i < count && 2 + i * 2 + 1 < c.data.size(); ++i) {
                song.timbres.emplace_back(c.data[2 + i * 2], c.data[3 + i * 2]);
            }
        } else if (c.id == "EVNT") {
            song.events.assign(c.data.begin(), c.data.end());
        }
    }
    songs.push_back(std::move(song));
}

} // namespace

std::vector<XmidiSong> readXmidi(std::span<const std::uint8_t> bytes) {
    std::vector<XmidiSong> songs;
    for (const auto& top : chunks(bytes)) {
        if (top.id == "FORM" && top.data.size() >= 4 && std::memcmp(top.data.data(), "XMID", 4) == 0) {
            readSong(top.data, songs);
        } else if (top.id == "CAT " && top.data.size() >= 4) {
            for (const auto& inner : chunks(top.data.subspan(4))) {
                if (inner.id == "FORM") {
                    readSong(inner.data, songs);
                }
            }
        }
    }
    if (songs.empty()) {
        throw FormatError("not a music file");
    }
    return songs;
}

} // namespace helmsman::audio
