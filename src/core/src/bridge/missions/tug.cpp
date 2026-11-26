// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second mission on the bridge: orders to Beta Myamid, an Elasi pirate
// on the way in, the captured U.S.S. Masada, and talks with Elasi Cereth
// while the landing party needs the Masada's shields down.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,
    kArrived = 1,  // at Beta Myamid, the pirate comes
    kPirateGone = 2,
    kAtMasada = 3, // in orbit beside the Masada
    kDone = 4,
};

// The script's notes.
enum Var : int {
    kHostagesKilled = 0,
    kShieldsDown = 1,  // the prefix code lowered the Masada's shields
    kKnowsPrefix = 2,  // Spock suggested the prefix code
    kTalked = 3,       // Elasi Cereth has spoken to Kirk
};

const char* kCereth = "Elasi Cereth";

void cereth(Bridge& b, int line) {
    b.showComm("pira");
    b.sayAs(kCereth, b.text(line), 0xA0, 0xBE, 0x2C);
}

void oneFewer(Bridge& b) { spock(b, 60); }

// The first talk with Elasi Cereth.
void firstContact(Bridge& b) {
    Voyage& v = b.voyage();
    b.showComm("pira");
    b.sayAs(kCereth, b.text(65), 0xA0, 0xBE, 0x2C);
    auto demands = [&b] {
        cereth(b, 44);
        uhura(b, 45);
    };
    switch (b.choose({66, 67, 68})) {
    case 1:
        v.vars[kHostagesKilled] += 1;
        cereth(b, 46);
        oneFewer(b);
        break;
    case 2:
        cereth(b, 47);
        demands();
        break;
    default:
        demands();
        break;
    }
    // Both answers here get the same reply in the original.
    b.choose({48, 49});
    cereth(b, 51);
    scott(b, 52);
    sulu(b, 53);
    cereth(b, 54);
    switch (b.choose({55, 56, 57})) {
    case 0:
        break;
    case 1:
        cereth(b, 69);
        break;
    default:
        cereth(b, 70);
        v.vars[kHostagesKilled] += 1;
        oneFewer(b);
        cereth(b, 71);
        break;
    }
    b.hideComm();
}

void finish(Bridge& b) {
    Voyage& v = b.voyage();
    v.clock = 0;
    v.stage = kDone;
}

} // namespace

void tugEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        v.destination = 0xE;
        if (v.system == v.destination) {
            v.clock = 0;
            v.stage = kArrived;
            tugEvent(b, e);
            return;
        }
        if (e.type == kTick) {
            switch (e.what) {
            case 1:
                b.session().loadMusic("bridge");
                v.awayScore = 0;
                v.awayBest = 0x20;
                v.awayExtra = 0;
                b.resetShip();
                v.textFile = "tug";
                v.vars = {};
                b.flyby("hijacked", "TUG0\\FLYBY", kStart);
                break;
            case 0x96:
                b.music(0);
                b.sound(0x22);
                uhura(b, 0);
                kirk(b, 1);
                break;
            case 0xAA:
                b.comm("woman", "Admiral", b.text(2));
                break;
            case 0xAF:
                chekov(b, 3);
                break;
            default:
                break;
            }
        } else if (e.type == kOrder && e.what == kHail) {
            uhura(b, 4);
            return;
        } else if (e.type == kOrder && e.what == kSpockTalk) {
            spock(b, 5);
            return;
        }
        b.defaultEvent(e);
        return;

    case kArrived:
        if (e.type == kTick) {
            switch (e.what) {
            case 0x20:
                spock(b, 6);
                break;
            case 0x40:
                b.sound(0x22);
                uhura(b, 7);
                kirk(b, 8);
                b.showComm("pira");
                b.sayAs("Elasi Captain", b.text(9), 0xA0, 0xBE, 0x2C);
                kirk(b, 10);
                b.showComm("pira");
                b.sayAs("Elasi Captain", b.text(11), 0xA0, 0xBE, 0x2C);
                b.hideComm();
                b.sound(0x28);
                break;
            case 0x80:
                b.space().addShip("ORION");
                v.underAttack = true;
                b.music(2);
                break;
            default:
                break;
            }
        } else if (e.type == kOrder) {
            switch (e.what) {
            case kOrbit:
                return;
            case kHail:
                uhura(b, 12);
                return;
            case kSpockTalk:
                spock(b, 13);
                return;
            default:
                break;
            }
        }
        if (v.underAttack && present(b, 0) && belowHalf(ship(b, 0), space::kHull)) {
            b.music(3);
            chekov(b, 14);
            spock(b, 15);
            space::Object& pirate = ship(b, 0);
            for (int i = space::kPortTorpedoes; i <= space::kStarboardPhasers; ++i) {
                pirate.cls.parts[static_cast<std::size_t>(i)].on = false;
            }
            pirate.behaviour = 1;
            v.clock = 0;
            v.stage = kPirateGone;
        }
        b.defaultEvent(e);
        return;

    case kPirateGone:
        if (e.type == kOrder) {
            switch (e.what) {
            case kOrbit:
                if (present(b, 0)) {
                    b.space().remove(space::Space::kFirstShip);
                }
                b.approach(6);
                v.clock = 0;
                v.stage = kAtMasada;
                return;
            case kHail:
                uhura(b, 16);
                return;
            case kSpockTalk:
                spock(b, 17);
                return;
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kAtMasada:
        if (e.type == kTick && e.what == 0x10) {
            b.sayAt(kSpock, b.text(20), 0xA0, 0xBE, 0x2C);
        }
        if (e.type == kBeamedUp) {
            finish(b);
            return;
        }
        if (e.type == kOrder) {
            switch (e.what) {
            case kHail: {
                const int choice = v.vars[kKnowsPrefix] == 1 ? b.choose({18, 19}) : 0;
                if (choice == 0) {
                    uhura(b, 21);
                    if (v.vars[kTalked] == 0) {
                        firstContact(b);
                        v.vars[kTalked] = 1;
                    } else {
                        cereth(b, 22);
                        kirk(b, 23);
                        b.hideComm();
                        v.vars[kHostagesKilled] += 1;
                        oneFewer(b);
                    }
                    if (v.vars[kHostagesKilled] > 4) {
                        v.awayExtra = 2;
                        finish(b);
                    }
                } else if (choice == 1) {
                    if (b.enterCode() == "293391-197736-3829") {
                        uhura(b, 25);
                        v.vars[kShieldsDown] = 1;
                    } else {
                        uhura(b, 26);
                    }
                }
                return;
            }
            case kSpockTalk:
                spock(b, 27);
                v.vars[kKnowsPrefix] = 1;
                return;
            case kTransporter:
                if (v.vars[kShieldsDown] == 0) {
                    b.sayAt(kSulu, b.text(28), 0x7A, 0x74, 0xA1);
                    return;
                }
                break;
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
                if (v.awayExtra != 2) {
                    b.placeCrew(kSpock, "xstndsp");
                    b.placeCrew(kMcCoy, "xstndmc");
                    if (v.awayExtra == 1) {
                        b.removeStill();
                    }
                }
            } else if (v.clock == 0x14) {
                if (v.awayExtra == 0) {
                    b.sound(0x22);
                    uhura(b, 29);
                    kirk(b, 30);
                    b.score(kTug, 31, v.awayScore, v.awayBest);
                    mccoy(b, 32);
                    spock(b, 33);
                    mccoy(b, 34);
                    kirk(b, 35);
                    spock(b, 36);
                    kirk(b, 37);
                } else if (v.awayExtra == 1) {
                    b.sound(0x22);
                    uhura(b, 39);
                    kirk(b, 40);
                    b.score(kTug, 41, 0, 0);
                } else {
                    b.sound(0x22);
                    uhura(b, 42);
                    b.score(kTug, 43, 0, 0);
                }
                b.removeStill();
                b.orbit(false, false);
                b.placeCrew(kSpock, "bstndsp");
                b.removeCrew(kMcCoy);
                v.clock = 0;
                v.mission = kLove;
                v.stage = 0;
                v.busy = false;
                b.clearEvents();
                return;
            }
            b.defaultEvent(e);
        }
        return;

    default:
        b.defaultEvent(e);
        return;
    }
}

} // namespace helmsman::bridge
