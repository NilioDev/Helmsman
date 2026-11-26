// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the disc image readers share (see disc.cpp, disc_formats.cpp and
// disc_music.cpp).
#pragma once

#include "helmsman/data/disc.hpp"
#include "helmsman/data/reader.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace helmsman {

struct Disc::Store {
    virtual ~Store() = default;
    virtual std::uint64_t size() const = 0;
    // Reads `n` bytes from `at` on. Throws FormatError past the end or when
    // the bytes cannot be read.
    virtual void read(std::uint64_t at, std::uint8_t* out, std::size_t n) = 0;
};

// What a format reader fills in.
struct DiscBuilder {
    Disc& disc;
    std::size_t addStore(std::unique_ptr<Disc::Store> store);
    void addTrack(const Disc::Track& track);
    std::size_t trackCount() const { return disc.tracks_.size(); }
};

// A plain file, read as it is.
std::unique_ptr<Disc::Store> fileStore(const std::filesystem::path& path);
// A music file (.wav, .ogg, .flac, .mp3), as the CD's samples: 16-bit
// stereo at 44100 Hz, the left first.
std::unique_ptr<Disc::Store> musicStore(const std::filesystem::path& path);
// Whether a cue sheet's file is a music file rather than raw sectors.
bool isMusicFile(const std::filesystem::path& path, const std::string& cueType);

// The readers, one for each kind of image.
void readCueSheet(DiscBuilder& b, const std::filesystem::path& cue);
void readCloneCd(DiscBuilder& b, const std::filesystem::path& ccd);
void readAlcohol(DiscBuilder& b, const std::filesystem::path& mds);
void readNero(DiscBuilder& b, const std::filesystem::path& nrg);
void readChd(DiscBuilder& b, const std::filesystem::path& chd);

// A file beside `near` named `name`, also when the case of its name
// differs; never a path out of that folder. Throws FormatError when it is
// missing.
std::filesystem::path besideFile(const std::filesystem::path& near, const std::string& name);
// The same, or an empty path when there is none.
std::filesystem::path findBeside(const std::filesystem::path& near, const std::string& name);
std::string lowerText(std::string text);
std::uint64_t fileSizeOf(const std::filesystem::path& path);

} // namespace helmsman
