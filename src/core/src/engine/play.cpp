// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/play.hpp"

#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"

namespace helmsman {

void playGame(Platform& platform, GameSource& source) {
    // For now: the bridge, until a key is pressed.
    const auto archive = Archive::open(source, "DATA");
    const char* picture = source.game() == GameId::Anniversary ? "BRIDGE" : "TITLE";
    if (archive.has(std::string(picture) + ".BMP") && archive.has(std::string(picture) + ".PAL")) {
        platform.palette = Palette::fromBytes(archive.need(std::string(picture) + ".PAL"));
        Bitmap::fromBytes(archive.need(std::string(picture) + ".BMP")).copyTo(platform.screen, 0, 0);
    }
    platform.pointerTargets.clear();
    platform.takeSkip();
    while (!platform.takeSkip()) {
        platform.waitFrame();
    }
}

} // namespace helmsman
