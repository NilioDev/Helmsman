// SPDX-License-Identifier: GPL-3.0-or-later
//
// The third mission on the bridge: a raid across the Romulan Neutral Zone
// puts research station ARK7 in danger. A cloaked Romulan waits on the way
// in; at the station, a Romulan centurion and an autobeacon.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,
    kToArk7 = 1,
    kAmbush = 2,
    kAfterAmbush = 3,
    kAtArk7 = 4,
    kDone = 5,
};

enum Var : int {
    kWait = 0,       // a box is open that the script waits on
    kPreaxSpoke = 1,
};

} // namespace

void loveEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridge");
                b.resetShip();
                v.textFile = "love";
                v.vars = {};
                b.flyby("tlove", "LOV0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                b.sound(0x22);
                uhura(b, 0);
                kirk(b, 1, 1);
            }
        } else if (e.type == kSaid && e.what == 1) {
            b.comm("woman", "Admiral", b.text(2));
            v.clock = 0;
            v.stage = kToArk7;
            v.destination = 2;
            b.setControl(true);
        }
        b.defaultEvent(e);
        return;

    case kToArk7:
        if (!v.underAttack && b.song() == 2) {
            b.music(-1);
        }
        if (e.type == kOrder) {
            switch (e.what) {
            case kStarMap: {
                const bool was = v.system == v.destination;
                b.defaultEvent(e);
                if (v.system == v.destination && !was) {
                    spock(b, 3, 1);
                    v.vars[kWait] = 1;
                }
                return;
            }
            case kSpockTalk:
                if (v.vars[kWait] == 0) {
                    spock(b, 4);
                }
                return;
            case kHail:
                if (v.underAttack) {
                    break;
                }
                if (v.vars[kWait] == 0) {
                    uhura(b, 5);
                }
                return;
            default:
                if (v.vars[kWait] != 0) {
                    return;
                }
                break;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kSaid && e.what == 1) {
            v.clock = 0;
            v.stage = kAmbush;
        }
        b.defaultEvent(e);
        return;

    case kAmbush:
        if (e.type == kTick) {
            if (v.clock == 1) {
                const int i = b.space().addShip("ROMULAN");
                if (i >= 0) {
                    b.space().object(i).cls.parts[space::kPortTorpedoes].on = false;
                }
                b.sayAt(kSpock, "#BRID\\B_160#Romulan ship decloaking, Captain.", 0x126, 0x6A, 0x2C);
                b.sound(0x22);
                uhura(b, 6, 1);
            } else if (!v.underAttack && v.clock > 1) {
                v.clock = 0;
                v.stage = kAfterAmbush;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder && (e.what == kHail || e.what == kSpockTalk)) {
            if (v.vars[kWait] == 0) {
                if (e.what == kHail) {
                    uhura(b, 7);
                } else {
                    spock(b, 8);
                }
            }
            return;
        }
        if (e.type == kSaid && e.what == 1) {
            v.vars[kWait] = 0;
            b.comm("romula", "Romulan Captain", b.text(9));
            spock(b, 10);
            if (present(b, 0)) {
                ship(b, 0).cls.parts[space::kPortTorpedoes].on = true;
            }
            b.music(2);
            b.sound(0x28);
            return;
        }
        if (v.vars[kWait] == 0) {
            b.defaultEvent(e);
        }
        return;

    case kAfterAmbush:
        if (e.type == kTick) {
            if (v.clock == 1) {
                spock(b, 11);
                sulu(b, 12, 1);
                v.vars[kWait] = 1;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kSaid) {
            if (e.what == 1) {
                b.approach(4);
                v.clock = 0;
                v.stage = kAtArk7;
                v.vars[kWait] = 0;
            }
            return;
        }
        if (v.vars[kWait] == 0) {
            b.defaultEvent(e);
        }
        return;

    case kAtArk7:
        if (e.type == kTick && v.clock == 0xA) {
            uhura(b, 17);
            b.post({kSaid, 2});
        } else if (e.type == kOrder && e.what == kSpockTalk) {
            spock(b, 13);
            return;
        } else if (e.type == kOrder && e.what == kHail && v.vars[kPreaxSpoke] == 0) {
            b.sound(0x22);
            uhura(b, 14, 1);
            return;
        } else if (e.type == kSaid && e.what == 1) {
            b.comm("romula", "Centurion Preax", b.text(15));
            v.vars[kPreaxSpoke] = 1;
        } else if (e.type == kSaid && e.what == 2) {
            b.sayAs("Autobeacon", b.text(16), 0xA0, 0x82, 0xA1);
        } else if (e.type == kBeamedUp) {
            v.clock = 0;
            v.stage = kDone;
            return;
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
                uhura(b, 18);
                kirk(b, 19, 1);
            } else if (v.clock == 0x208) {
                kirk(b, 20);
                mccoy(b, 21);
                kirk(b, 22);
                mccoy(b, 23);
                scott(b, 24);
                kirk(b, 25, 2);
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kSaid && e.what == 1) {
            b.score(kLove, 26, v.awayScore, v.awayBest);
            v.clock = 0x1F4;
        } else if (e.type == kSaid && e.what == 2) {
            b.removeStill();
            b.orbit(false, false);
            v.mission = kMudd;
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
