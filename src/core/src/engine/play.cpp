// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/play.hpp"

#include "helmsman/sequel/game.hpp"
#include "helmsman/sequel/scenes.hpp"

#include "helmsman/away/mission.hpp"
#include "helmsman/bridge/bridge.hpp"
#include "helmsman/bridge/transporter.hpp"
#include "helmsman/bridge/voyage.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/engine/savegame.hpp"
#include "helmsman/engine/titles.hpp"
#include "helmsman/gfx/bitmap.hpp"

#include <cctype>
#include <optional>
#include <string>

namespace helmsman {

namespace {

std::string upperName(int mission) {
    std::string name = bridge::missionName(mission);
    for (char& c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name;
}

enum class Next {
    Quit,
    Restart,
};

// One voyage, from the first mission (or a saved game) until it ends.
Next voyage(away::Session& session, int load) {
    Platform& platform = session.platform;
    bridge::Voyage voyage;
    bridge::Bridge ship(session, voyage);
    session.saveVoyage = [&voyage, &ship](SaveWriter& w) {
        voyage.save(w);
        ship.saveState(w);
    };
    session.playEffect("BRIDLOOP", true);
    while (!platform.quit) {
        std::optional<away::MissionResult> away;
        if (load >= 0) {
            const auto data = readSave(platform.saveFolder, "anniversary", load);
            load = -1;
            if (data) {
                SaveReader r(*data);
                const std::string kind = r.text();
                session.restore(r);
                if (r.u8() == 0) {
                    throw FormatError("a saved game of one mission alone");
                }
                voyage.restore(r);
                ship.restoreState(r);
                if (kind == "away") {
                    session.stopEffects();
                    away::Mission m(session, upperName(voyage.mission));
                    away = m.resume(r);
                } else if (kind != "bridge") {
                    throw FormatError("a saved game of a part Helmsman does not play yet");
                }
            }
        }
        if (!away) {
            const bridge::BridgeResult r = ship.run();
            switch (r.leave) {
            case bridge::Leave::BeamDown:
                break;
            case bridge::Leave::LoadSave:
                load = r.loadSave;
                continue;
            case bridge::Leave::Restart:
                return Next::Restart;
            default: // quit, or the end of the last mission
                return Next::Quit;
            }
            session.loadMusic("ground");
            bridge::transporter(session, true, 4);
            away = away::playAway(session, upperName(voyage.mission), 0, 4);
        }
        if (away->quit) {
            return Next::Quit;
        }
        if (away->restart) {
            return Next::Restart;
        }
        if (away->loadSave >= 0) {
            load = away->loadSave;
            continue;
        }
        voyage.awayScore = away->score;
        voyage.awayBest = away->a;
        voyage.awayExtra = away->b;
        bridge::transporter(session, false, 4);
        ship.clearEvents();
        ship.post({bridge::kBeamedUp, 0});
        session.stopEffects();
        session.playEffect("BRIDLOOP", true);
    }
    return Next::Quit;
}

// The first game: the bridge, and the planets the crew beams down to.
void playAnniversary(Platform& platform, GameSource& source) {
    {
        away::Session titles(platform, source);
        playOpeningTitles(titles);
    }
    Next next = Next::Restart;
    while (next == Next::Restart && !platform.quit) {
        away::Session session(platform, source); // all afresh
        next = voyage(session, -1);
    }
}

} // namespace

void playGame(Platform& platform, GameSource& source) {
    if (source.game() == GameId::Anniversary) {
        playAnniversary(platform, source);
        return;
    }
    sequel::Assets assets(source);
    sequel::playScene(platform, assets, "TITLE");
    if (platform.quit) {
        return;
    }
    sequel::playGame(platform, assets);
}

} // namespace helmsman
