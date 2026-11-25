// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures and animations of the planet missions, as the first game
// keeps them: most of the crew's pictures are stored only for the doctor,
// with the other three as a difference (.XOR) laid over his; and every
// picture can be shown smaller or larger by the depth it stands at. See
// docs/formats/crew-pictures.md.
#pragma once

#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman::away {

// One frame of an animation (.ANM).
struct AnimFrame {
    std::string picture; // the picture's name, without ".BMP"
    int x = 0;           // where it is drawn, from the actor's position
    int y = 0;
    int depth = 0;    // added to the depth of the actor's row
    int ticks = 0;    // how long it shows
    std::uint8_t next[4] = {}; // the frame that follows, one chosen at random
};

std::vector<AnimFrame> parseAnim(std::span<const std::uint8_t> bytes);

// Scales a picture to scale/256 of its size, picking rows and columns the
// way the game does.
Bitmap scaleBitmap(const Bitmap& from, int scale);

class Assets {
public:
    explicit Assets(const Archive& archive) : archive_(archive) {}

    const Archive& archive() const { return archive_; }

    // A picture by its name (any case, no extension), building the crew's
    // pictures from the doctor's and a difference when needed. Throws
    // FormatError when there is none.
    std::shared_ptr<const Bitmap> picture(std::string_view name);
    // The same at scale/256 of its size.
    std::shared_ptr<const Bitmap> picture(std::string_view name, int scale);
    bool hasPicture(std::string_view name) const;

    // An animation; empty when it is missing.
    const std::vector<AnimFrame>& anim(std::string_view name);

    void clear() {
        pictures_.clear();
        scaled_.clear();
        anims_.clear();
    }

private:
    Bitmap buildCrewPicture(const std::string& name);

    const Archive& archive_;
    std::map<std::string, std::shared_ptr<const Bitmap>> pictures_;
    std::map<std::pair<std::string, int>, std::shared_ptr<const Bitmap>> scaled_;
    std::map<std::string, std::vector<AnimFrame>> anims_;
};

std::string upper(std::string_view s);
std::string lower(std::string_view s);

} // namespace helmsman::away
