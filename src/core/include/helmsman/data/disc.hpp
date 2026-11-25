// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading the player's own game CD from an image of it: a cue sheet with
// its .bin files, a single raw .bin, or a plain .iso. Helmsman reads the
// game's files straight out of the image, and the CD audio tracks too.
// See docs/formats/disc.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

struct DiscFile {
    std::string path; // folders and name, upper case, '/' between, no ";1"
    std::uint32_t sector = 0;
    std::uint32_t size = 0;
};

// One audio track: where its samples lie in which file. Samples are 16-bit
// stereo at 44100 Hz, the left first.
struct AudioTrack {
    int number = 0;
    std::filesystem::path file;
    std::uint64_t offset = 0; // bytes into the file
    std::uint64_t size = 0;   // bytes
};

class Disc {
public:
    static constexpr std::uint32_t kSectorData = 2048;
    static constexpr std::uint32_t kRawSector = 2352;

    // Opens a .cue, .bin or .iso. Throws FormatError when it is not a CD
    // image of a data disc or is damaged.
    static std::unique_ptr<Disc> open(const std::filesystem::path& path);

    const std::vector<DiscFile>& files() const { return files_; }
    const std::vector<AudioTrack>& audioTracks() const { return audio_; }

    // Finds a file by its path, ignoring case; '/' or '\' between folders.
    const DiscFile* find(std::string_view path) const;

    // Reads a whole file. Throws FormatError on a read failure.
    std::vector<std::uint8_t> read(const DiscFile& file);
    // Reads `size` bytes from `offset` on; fewer where the file ends.
    std::vector<std::uint8_t> read(const DiscFile& file, std::uint64_t offset, std::size_t size);

    // Reads `count` 16-bit stereo frames of an audio track from `frame` on.
    std::vector<std::int16_t> readAudio(const AudioTrack& track, std::uint64_t frame, std::size_t count);

private:
    void readSector(std::uint32_t sector, std::uint8_t* out);
    void readFolder(std::uint32_t sector, std::uint32_t size, const std::string& prefix, int depth);

    std::filesystem::path dataPath_;
    std::ifstream data_;
    std::uint64_t dataSize_ = 0;
    std::uint32_t rawSize_ = kSectorData;
    std::uint32_t dataStart_ = 0;
    std::vector<DiscFile> files_;
    std::vector<AudioTrack> audio_;
    std::ifstream audioFile_;
    std::filesystem::path audioOpen_;
};

// Makes a path upper case with '/' between its parts.
std::string normalPath(std::string_view path);

} // namespace helmsman
