// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first mission on the bridge: a mock battle with the U.S.S. Republic,
// then orders to Pollux V, the High Prelate's welcome, and Starfleet's
// verdict once the landing party is back.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,      // the flyby, the captain's log, the Republic comes
    kBattle = 1,     // the mock battle
    kAfterBattle = 2,
    kToPollux = 3,
    kDone = 4,       // back from the planet
};

void endBattle(Bridge& b) {
    Voyage& v = b.voyage();
    b.clearShots();
    b.resetShip();
    b.sayAt(kSulu, "#BRID\\B_336#Lowering shields and disarming weapons.", 0x7A, 0x74, 0xB0, 2);
    // The Republic stands down and leaves.
    space::Object& republic = ship(b, 0);
    for (int i = space::kPortTorpedoes; i <= space::kStarboardPhasers; ++i) {
        republic.cls.parts[static_cast<std::size_t>(i)].on = false;
    }
    republic.behaviour = 1;
    v.clock = 0;
    v.stage = kAfterBattle;
}

} // namespace

void demonEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridgew");
                b.resetShip();
                v.textFile = "demon";
                v.vars = {};
                b.flyby("demon", "DEM0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                b.sayAs("Captain's Log", b.text(0), 0xA0, 0x82, 0xB0);
            } else if (v.clock == 0xA5) {
                uhura(b, 1);
                spock(b, 2);
                b.space().addShip("ENTERPRI");
                v.stage = kBattle;
                b.setControl(true);
                b.music(2);
                b.sound(0x28);
            }
        }
        b.defaultEvent(e);
        return;

    case kBattle: {
        const space::Object& player = b.space().player();
        if (present(b, 0) && belowHalf(ship(b, 0), space::kHull)) {
            b.music(3);
            spock(b, 4);
            uhura(b, 5);
            endBattle(b);
            demonEvent(b, e);
            return;
        }
        if (belowHalf(player, space::kHull)) {
            b.music(4);
            spock(b, 6);
            scott(b, 7);
            kirk(b, 8);
            uhura(b, 9);
            endBattle(b);
            demonEvent(b, e);
            return;
        }
        if (e.type == kOrder && e.what == kHail) {
            uhura(b, 10);
            return;
        }
        if (e.type == kOrder && e.what == kSpockTalk) {
            spock(b, 11);
            return;
        }
        b.defaultEvent(e);
        return;
    }

    case kAfterBattle:
        if (e.type == kTick && v.clock == 0x14) {
            b.sound(0x22);
            uhura(b, 13);
            kirk(b, 14, 1);
        } else if (e.type == kOrder) {
            return; // the crew is busy with the Republic
        } else if (e.type == kSaid && e.what == 2) {
            b.space().player().shieldsUp = false;
            b.space().player().weaponsArmed = false;
            b.setControl(true);
        } else if (e.type == kSaid && e.what == 1) {
            if (present(b, 0)) {
                b.space().remove(space::Space::kFirstShip);
            }
            b.comm("woman", "Admiral", b.text(12));
            v.clock = 0;
            v.stage = kToPollux;
            v.destination = 0x12;
        }
        b.defaultEvent(e);
        return;

    case kToPollux:
        if (e.type == kBeamedUp) {
            v.clock = 0;
            v.stage = kDone;
            return;
        }
        if (e.type == kOrder) {
            const bool here = v.system == v.destination;
            switch (e.what) {
            case kStarMap: {
                b.defaultEvent(e);
                if (v.system == v.destination && !here) {
                    spock(b, 15);
                }
                return;
            }
            case kHail:
                if (v.underAttack) {
                    break;
                }
                if (!here) {
                    uhura(b, 16);
                } else if (v.vars[0] == 0) {
                    b.sound(0x22);
                    uhura(b, 17);
                    b.comm("prst", "Priest", b.text(18));
                    v.vars[0] = 1;
                } else {
                    uhura(b, 19);
                }
                return;
            case kSpockTalk:
                if (v.underAttack) {
                    break;
                }
                if (!here) {
                    spock(b, 20);
                } else if (!v.inOrbit) {
                    spock(b, 21);
                } else {
                    spock(b, 22);
                }
                return;
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kDone:
        if (e.type == kTick) {
            if (v.clock == 1) {
                v.busy = true;
                b.placeCrew(kSpock, "xstndsp");
                b.placeCrew(kMcCoy, "xstndmc");
            } else if (v.clock == 0xA) {
                b.sound(0x22);
                uhura(b, 23);
                kirk(b, 24, 1);
            } else if (v.clock == 0x208) {
                mccoy(b, 25);
                kirk(b, 26);
                spock(b, 27);
                mccoy(b, 28, 2);
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kSaid && e.what == 1) {
            b.score(kDemon, 29, v.awayScore, v.awayBest);
            v.clock = 0x1F4;
        } else if (e.type == kSaid && e.what == 2) {
            b.orbit(false, false);
            v.mission = kTug;
            v.stage = 0;
            v.clock = 0;
            b.placeCrew(kSpock, "bstndsp");
            b.removeCrew(kMcCoy);
            v.busy = false;
            b.clearEvents();
        }
        return;

    default:
        b.defaultEvent(e);
        return;
    }
}

} // namespace helmsman::bridge
