// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's painted animations (.ANM, "LPF" files): full-screen
// pictures where each frame is drawn over the one before. See
// docs/formats/animation.md.
#pragma once

#include "helmsman/gfx/palette.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace helmsman {

class Animation {
public:
    // Takes the whole file. Throws FormatError when it is no animation.
    explicit Animation(std::vector<std::uint8_t> file);

    int width() const { return width_; }
    int height() const { return height_; }
    int frameCount() const { return frames_; }
    int framesPerSecond() const { return fps_; }
    const Palette& palette() const { return palette_; }

    // Draws the next frame (the first on the first call). After the last
    // frame the film starts over. Throws FormatError on damaged data.
    void next();
    // The frame drawn last: width() * height() colour numbers.
    const std::vector<std::uint8_t>& picture() const { return picture_; }
    // The number of the frame drawn last, from 0.
    int frame() const { return frame_; }

    static bool isAnimation(std::span<const std::uint8_t> bytes);

private:
    struct Page {
        int first = 0;
        int count = 0;
    };
    std::span<const std::uint8_t> record(int index) const;
    void draw(std::span<const std::uint8_t> record);

    std::vector<std::uint8_t> file_;
    int width_ = 0;
    int height_ = 0;
    int frames_ = 0;
    int records_ = 0;
    int fps_ = 0;
    Palette palette_;
    std::vector<Page> pages_;
    std::vector<std::uint8_t> picture_;
    int frame_ = -1;
};

} // namespace helmsman
