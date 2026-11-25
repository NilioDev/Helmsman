// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the player's own copy of a game is: an image of its CD, or a folder
// with the CD's files (or the game installed from it). Both are read the
// same way, by paths relative to the folder that holds the game's program,
// without regard to case.
#pragma once

#include "helmsman/data/disc.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman {

// The games Helmsman plays.
enum class GameId : std::uint8_t {
    Anniversary,    // the 1992 game, Enhanced CD-ROM version
    JudgmentRites,  // its 1993 sequel, CD-ROM version
};

class GameSource {
public:
    virtual ~GameSource() = default;

    GameId game() const { return game_; }
    // What the player gave: the image or the folder.
    const std::filesystem::path& origin() const { return origin_; }

    // Reads a file by its path from the game's folder ('/' or '\' between
    // folders, any case). Empty when there is no such file. Throws
    // FormatError when it cannot be read.
    virtual std::optional<std::vector<std::uint8_t>> read(std::string_view path) = 0;
    virtual bool exists(std::string_view path) const = 0;
    // Reads `size` bytes of a file from `offset` on.
    virtual std::vector<std::uint8_t> readPart(std::string_view path, std::uint64_t offset, std::size_t size) = 0;
    virtual std::uint64_t sizeOf(std::string_view path) const = 0;

    // The CD's audio tracks, when the source is a full image; none else.
    virtual std::vector<AudioTrack> audioTracks() const { return {}; }
    virtual std::vector<std::int16_t> readAudio(const AudioTrack&, std::uint64_t, std::size_t) { return {}; }

    // Opens an image (.cue, .bin, .iso) or a folder and finds which game it
    // holds. Empty when it holds neither; throws FormatError when an image
    // is damaged.
    static std::unique_ptr<GameSource> open(const std::filesystem::path& path);

protected:
    GameId game_ = GameId::Anniversary;
    std::filesystem::path origin_;
};

// The program file that tells each game apart.
std::string_view programName(GameId game);

} // namespace helmsman
