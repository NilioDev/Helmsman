// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/play.hpp"

#include "helmsman/away/mission.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"

namespace helmsman {

void playGame(Platform& platform, GameSource& source) {
    if (source.game() == GameId::Anniversary) {
        // For now: the first planet mission, the crew beaming down.
        away::Session session(platform, source);
        away::Mission mission(session, "DEMON");
        mission.run(0, 4);
        return;
    }
    const auto archive = Archive::open(source, "DATA");
    const char* picture = "TITLE";
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
