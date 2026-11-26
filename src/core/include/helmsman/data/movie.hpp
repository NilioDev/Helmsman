// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's films (.MVE): pictures in 8x8 blocks that are copied
// from earlier pictures or given as raw pixels, with 8-bit sound beside them.
// See docs/formats/movie.md.
#pragma once

#include "helmsman/gfx/palette.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace helmsman {

class Movie {
public:
    // Takes the whole file. Throws FormatError when it is no film.
    explicit Movie(std::vector<std::uint8_t> file);

    // Reads on to the next picture. Returns false at the end of the film.
    // Throws FormatError on damaged data.
    bool next();

    int width() const { return width_; }
    int height() const { return height_; }
    // The picture shown now, width() * height() colour numbers.
    const std::vector<std::uint8_t>& picture() const { return shown_; }
    const Palette& palette() const { return palette_; }
    // How long each picture stays, in microseconds (0 before the first).
    int frameMicros() const { return frameMicros_; }

    // The sound rate, or 0 when the film has no sound this player plays.
    int soundRate() const { return soundRate_; }
    // The sound read since the last call: unsigned 8-bit mono.
    std::vector<std::uint8_t> takeSound();

    // Limits that keep a damaged file from asking for huge buffers.
    static constexpr int kMaxBlocksWide = 80;
    static constexpr int kMaxBlocksHigh = 60;

private:
    void videoInit(std::span<const std::uint8_t> body);
    void soundInit(std::span<const std::uint8_t> body, int version);
    void sound(std::span<const std::uint8_t> body, bool silence);
    void setPalette(std::span<const std::uint8_t> body);
    // Draws one picture. `map` holds a code per changed block, `changed`
    // says which blocks have one, `raw` the pixels.
    void decode(std::span<const std::uint16_t> map, const std::vector<bool>& changed,
                std::span<const std::uint8_t> raw);

    std::vector<std::uint8_t> file_;
    std::size_t pos_ = 0;        // the next chunk
    std::size_t chunkEnd_ = 0;   // the end of the chunk being read
    std::size_t op_ = 0;         // the next operation in it
    bool ended_ = false;

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> shown_;
    std::vector<std::uint8_t> back_; // the picture shown before last
    Palette palette_;
    int frameMicros_ = 0;

    int soundRate_ = 0;
    int soundChannels_ = 1;
    bool soundWide_ = false;
    std::vector<std::uint8_t> sound_;

    // The block coding with a separate skip list (operations 14 and 15).
    std::vector<std::uint16_t> skipWords_;
    std::vector<std::uint16_t> codes_;
};

} // namespace helmsman
