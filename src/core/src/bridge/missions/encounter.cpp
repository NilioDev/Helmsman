// SPDX-License-Identifier: GPL-3.0-or-later
//
// Random encounters: every warp to a system other than the one the orders
// name ends among enemies. Klingons, Romulans or Elasi pirates, one to four
// ships, who call ten ticks later and fight until they are gone.
#include "bridge/missions.hpp"

#include <random>

namespace helmsman::bridge {

using namespace script;

namespace {

enum Kind : int {
    kKlingons = 1,
    kRomulans = 2,
    kElasi = 3,
};

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

int roll(int below) { return static_cast<int>(rng()() % static_cast<unsigned>(below)); }

} // namespace

void startEncounter(Bridge& b) {
    Voyage& v = b.voyage();
    v.encounter = roll(3) + 1;
    v.encounterStage = 10; // ticks until they call
    const int ships = roll(4) + 1;
    for (int i = 0; i < ships; ++i) {
        switch (v.encounter) {
        case kKlingons:
            b.space().addShip("klingon");
            break;
        case kRomulans:
            b.space().addShip(roll(2) == 0 ? "romulan" : "ckling");
            break;
        default: {
            static const char* kPirates[] = {"orion", "pirate1", "mpirate"};
            b.space().addShip(kPirates[roll(3)]);
            break;
        }
        }
    }
    static const char* kTexts[] = {"", "klingon", "romulan", "elasi"};
    v.textFile = kTexts[v.encounter];
    v.underAttack = true;
    b.session().loadMusic(ships >= 3 ? "bridgeb" : "bridge");
}

void encounterEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    if (v.encounterStage > 0) {
        if (e.type == kTick) {
            v.encounterStage -= 1;
        }
        b.defaultEvent(e);
        return;
    }
    if (v.encounterStage == 0) {
        v.encounterStage = -1;
        chekov(b, 0);
        spock(b, 1);
        const int who = v.encounter == kKlingons ? kSpock : v.encounter == kElasi ? kKirk : kSulu;
        b.sayAt(who, b.text(2), 0x7A, 0x74, 0xB0);
        scott(b, 3);
        b.sound(0x22);
        uhura(b, 4);
        kirk(b, 5);
        static const char* kPictures[] = {"", "klg1", "romula", "pira"};
        b.comm(kPictures[v.encounter], "Captain", b.text(6));
        b.music(2);
        b.sound(0x28);
        b.defaultEvent(e);
        return;
    }
    if (e.type == kOrder && e.what == kSpockTalk) {
        spock(b, 7);
        return;
    }
    if (e.type == kOrder && e.what == kHail) {
        uhura(b, 8);
        return;
    }
    if (!v.underAttack) {
        // All gone: back to the mission.
        v.encounter = 0;
        v.textFile = missionName(v.mission);
        b.session().loadMusic("bridge");
    }
    b.defaultEvent(e);
}

} // namespace helmsman::bridge
