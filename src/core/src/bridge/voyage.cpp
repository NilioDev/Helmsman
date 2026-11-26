// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/bridge/voyage.hpp"

#include <algorithm>

namespace helmsman::bridge {

const char* missionName(int mission) {
    static constexpr const char* kNames[kMissionCount] = {"demon", "tug", "love", "mudd", "feather", "trial", "sins", "veng"};
    return kNames[std::clamp(mission, 0, kMissionCount - 1)];
}

void Voyage::save(SaveWriter& w) const {
    w.u8(static_cast<std::uint8_t>(mission));
    w.u16(static_cast<std::uint16_t>(stage));
    w.u32(clock);
    for (const int v : vars) {
        w.s32(v);
    }
    w.s32(system);
    w.s32(destination);
    w.u8(inOrbit ? 1 : 0);
    w.u8(underAttack ? 1 : 0);
    w.u8(busy ? 1 : 0);
    w.u16(static_cast<std::uint16_t>(encounter));
    w.u16(static_cast<std::uint16_t>(encounterStage));
    w.text(textFile);
    w.u8(flyby ? 1 : 0);
    w.text(flybyTitle);
    w.text(flybyVoice);
    w.u16(static_cast<std::uint16_t>(flybyStage));
    w.s32(awayScore);
    w.s32(awayBest);
    w.s32(awayExtra);
    w.u8(redshirtDead ? 1 : 0);
    for (const int c : commendations) {
        w.s32(c);
    }
    w.s32(lastScored);
    w.s32(rating);
    w.s32(points);
    w.s32(planetSeed);
    w.s32(planetRamp);
}

void Voyage::restore(SaveReader& r) {
    mission = std::clamp<int>(r.u8(), 0, kMissionCount - 1);
    stage = r.u16();
    clock = r.u32();
    for (int& v : vars) {
        v = r.s32();
    }
    system = r.s32();
    destination = r.s32();
    inOrbit = r.u8() != 0;
    underAttack = r.u8() != 0;
    busy = r.u8() != 0;
    encounter = r.u16();
    encounterStage = r.u16();
    textFile = r.text();
    flyby = r.u8() != 0;
    flybyTitle = r.text();
    flybyVoice = r.text();
    flybyStage = r.u16();
    awayScore = r.s32();
    awayBest = r.s32();
    awayExtra = r.s32();
    redshirtDead = r.u8() != 0;
    for (int& c : commendations) {
        c = r.s32();
    }
    lastScored = r.s32();
    rating = r.s32();
    points = r.s32();
    planetSeed = r.s32();
    planetRamp = r.s32();
}

} // namespace helmsman::bridge
