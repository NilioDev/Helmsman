// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading the player's own game CD from an image of it. Helmsman knows the
// common kinds: a cue sheet with its files (also as GOG keeps it), a single
// .bin, .iso or .img, CloneCD (.ccd), Alcohol (.mds with .mdf), Nero (.nrg)
// and MAME's compressed .chd. It reads the game's files straight out of the
// image, and the CD audio tracks too. See docs/formats/disc-images.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

struct DiscFile {
    std::string path; // folders and name, upper case, '/' between, no ";1"
    std::uint32_t sector = 0;
    std::uint32_t size = 0;
};

// One audio track. Its samples are 16-bit stereo at 44100 Hz, the left
// first; `size` counts their bytes.
struct AudioTrack {
    int number = 0;
    std::size_t index = 0; // the disc's own number for it
    std::uint64_t size = 0;
};

class Disc {
public:
    static constexpr std::uint32_t kSectorData = 2048;
    static constexpr std::uint32_t kRawSector = 2352;

    // Whether a file's name looks like a disc image Helmsman can open.
    static bool isImage(const std::filesystem::path& path);

    // Opens a disc image. Throws FormatError when it is not an image of a
    // data CD, or is damaged.
    static std::unique_ptr<Disc> open(const std::filesystem::path& path);

    Disc();
    ~Disc();
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;

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

    // Where the bytes of a track come from: a file, a compressed image, a
    // decoded music file (see disc.cpp).
    struct Store;
    // A track: which store, where its first sector starts, how many bytes
    // each sector takes there, and where in a sector the data starts.
    struct Track {
        int number = 0;
        bool audio = false;
        std::size_t store = 0;
        std::uint64_t start = 0;
        std::uint32_t stride = kRawSector;
        std::uint32_t skip = 0;
        std::uint64_t sectors = 0; // 0: to the end of the store
        bool bigEndian = false;    // audio kept with the high byte first
    };

private:
    void readSector(std::uint32_t sector, std::uint8_t* out);
    void readFolder(std::uint32_t sector, std::uint32_t size, const std::string& prefix, int depth);
    void finish(); // finds the data track's layout and reads its file system

    std::vector<std::unique_ptr<Store>> stores_;
    std::vector<Track> tracks_;
    Track data_;
    std::uint64_t dataSectors_ = 0;
    std::vector<DiscFile> files_;
    std::vector<AudioTrack> audio_;

    friend struct DiscBuilder;
};

// Makes a path upper case with '/' between its parts.
std::string normalPath(std::string_view path);

} // namespace helmsman
