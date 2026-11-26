// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/play.hpp"

#include "helmsman/away/mission.hpp"
#include "helmsman/bridge/bridge.hpp"
#include "helmsman/bridge/transporter.hpp"
#include "helmsman/bridge/voyage.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/gfx/bitmap.hpp"

#include <cctype>
#include <string>

namespace helmsman {

namespace {

// The first game: the bridge, and the planets the crew beams down to.
void playAnniversary(Platform& platform, GameSource& source) {
    away::Session session(platform, source);
    bridge::Voyage voyage;
    bridge::Bridge ship(session, voyage);
    session.playEffect("BRIDLOOP", true);
    while (!platform.quit) {
        const bridge::BridgeResult r = ship.run();
        if (r.leave != bridge::Leave::BeamDown) {
            return;
        }
        session.loadMusic("ground");
        bridge::transporter(session, true, 4);
        std::string name = bridge::missionName(voyage.mission);
        for (char& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        const away::MissionResult away = away::playAway(session, name, 0, 4);
        if (away.quit) {
            return;
        }
        voyage.awayScore = away.score;
        voyage.awayBest = away.a;
        voyage.awayExtra = away.b;
        bridge::transporter(session, false, 4);
        ship.clearEvents();
        ship.post({bridge::kBeamedUp, 0});
        session.stopEffects();
        session.playEffect("BRIDLOOP", true);
    }
}

} // namespace

void playGame(Platform& platform, GameSource& source) {
    if (source.game() == GameId::Anniversary) {
        playAnniversary(platform, source);
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
