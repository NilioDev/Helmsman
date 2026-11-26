// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fourth mission on the bridge: orders to Harlequin, two Elasi pirates
// there, Harry Mudd's call for help, and the derelict near Harrapan that he
// is interested in.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,     // the orders, then the flight to Harlequin
    kHarlequin = 1, // the pirates, then Mudd's call
    kDerelict = 2,  // beside the derelict near Harrapan
};

enum Var : int {
    kOrdered = 0,    // the Admiral has given the orders
    kPirates = 1,    // the pirates have come
    kMuddCalled = 2, // Mudd has called; on to Harrapan
    kMapped = 3,     // the star map has been opened
    kBack = 4,       // the landing party is back
    kCountdown = 5,  // ticks until Starfleet's verdict
};

const char* kMuddName = "Harry Mudd";

void mudd(Bridge& b, int line) {
    b.showComm("mudd");
    b.sayAs(kMuddName, b.text(line), 0xA0, 0xBE, 0x2C);
}

void fleeWhenHurt(Bridge& b, int n) {
    if (!present(b, n)) {
        return;
    }
    space::Object& s = ship(b, n);
    const space::Part& hull = s.cls.parts[space::kHull];
    if (hull.now * 2 <= hull.best) {
        s.behaviour = 1;
    }
}

} // namespace

void muddEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (v.vars[kOrdered] == 0 && e.type != kTick && e.type != kSaid) {
            return;
        }
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridge");
                b.resetShip();
                v.textFile = "mudd";
                v.vars = {};
                b.flyby("another", "MUD0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                b.sound(0x22);
                uhura(b, 0);
                kirk(b, 1, 1);
            }
        } else if (e.type == kSaid && e.what == 1) {
            b.comm("woman", "Admiral", b.text(2));
            v.vars[kOrdered] = 1;
            v.destination = 3;
            b.setControl(true);
            return;
        } else if (e.type == kOrder) {
            switch (e.what) {
            case kHail:
                if (v.vars[kMapped] == 0) {
                    uhura(b, 3);
                    return;
                }
                break;
            case kSpockTalk:
                if (v.vars[kMapped] == 0) {
                    spock(b, 4);
                    return;
                }
                break;
            case kStarMap:
                v.vars[kMapped] = 1;
                b.defaultEvent(e);
                if (v.system == 3) {
                    spock(b, 5);
                    v.clock = 0;
                    v.stage = kHarlequin;
                }
                return;
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kHarlequin:
        if (e.type == kTick) {
            if (v.clock == 0xA) {
                v.vars[kPirates] = 0;
                spock(b, 6);
                b.sound(0x22);
                uhura(b, 7, 1);
            } else if (v.vars[kPirates] != 0 && !v.underAttack && v.vars[kMuddCalled] == 0) {
                if (b.song() >= 0) {
                    b.music(-1);
                }
                spock(b, 8);
                uhura(b, 9);
                kirk(b, 10);
                b.sayAs("Voice", b.text(11), 0xA0, 0x82, 0x2C);
                kirk(b, 12);
                uhura(b, 13, 2);
                v.vars[kMuddCalled] = 1;
                v.destination = 4;
            }
            if (v.underAttack) {
                fleeWhenHurt(b, 0);
                fleeWhenHurt(b, 1);
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kSaid && e.what == 1) {
            b.comm("pira", "Elasi Pirate", b.text(14));
            b.space().addShip("ORION");
            b.space().addShip("ORION");
            v.vars[kPirates] = 1;
            v.underAttack = true;
            b.music(2);
            return;
        }
        if (e.type == kSaid && e.what == 2) {
            mudd(b, 15);
            kirk(b, 16);
            mudd(b, 17);
            b.hideComm();
            spock(b, 18);
            return;
        }
        if (e.type == kOrder) {
            switch (e.what) {
            case kHail:
                if (v.underAttack) {
                    uhura(b, 19);
                    return;
                }
                if (v.vars[kMuddCalled] != 0) {
                    uhura(b, 20);
                    return;
                }
                break;
            case kSpockTalk:
                if (v.underAttack) {
                    spock(b, 21);
                    return;
                }
                if (v.vars[kMuddCalled] != 0) {
                    spock(b, 22);
                    return;
                }
                break;
            case kStarMap:
                if (v.vars[kMuddCalled] == 0) {
                    return;
                }
                b.defaultEvent(e);
                if (v.system == v.destination) {
                    b.approach(5);
                    v.clock = 0;
                    v.stage = kDerelict;
                }
                return;
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kDerelict:
        if (e.type == kTick) {
            if (v.clock == 0x64) {
                spock(b, 23);
                kirk(b, 24);
                spock(b, 25);
                b.sound(0x22);
                uhura(b, 26);
                mudd(b, 27);
                kirk(b, 28);
                mudd(b, 29);
                spock(b, 30);
                mudd(b, 31);
                kirk(b, 32);
                mudd(b, 33);
                kirk(b, 34);
                b.hideComm();
            }
            if (v.vars[kCountdown] != 0) {
                if (v.vars[kCountdown] == 1) {
                    b.score(kMudd, 35, v.awayScore, v.awayBest);
                    if (v.awayExtra != 0) {
                        scott(b, 36);
                        kirk(b, 37);
                    } else {
                        kirk(b, 38);
                    }
                    uhura(b, 41);
                    kirk(b, 42);
                    uhura(b, 43);
                    kirk(b, 44, 4);
                }
                v.vars[kCountdown] -= 1;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder) {
            if (v.vars[kBack] != 0) {
                return;
            }
            switch (e.what) {
            case kHail:
            case kStarMap:
                uhura(b, 45);
                return;
            case kSpockTalk:
                spock(b, 46);
                return;
            case kOrbit:
                return;
            default:
                break;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kBeamedUp) {
            v.vars[kBack] = 1;
            v.vars[kCountdown] = 10;
            b.placeCrew(kSpock, "xstndsp");
            b.placeCrew(kMcCoy, "xstndmc");
            v.busy = true;
            if (v.clock <= 0x64) {
                v.clock = 0x65;
            }
            return;
        }
        if (e.type == kSaid && e.what == 4) {
            b.removeStill();
            b.orbit(false, false);
            v.mission = kFeather;
            v.stage = 0;
            v.clock = 0;
            b.placeCrew(kSpock, "bstndsp");
            b.removeCrew(kMcCoy);
            v.busy = false;
            b.clearEvents();
            return;
        }
        b.defaultEvent(e);
        return;

    default:
        b.defaultEvent(e);
        return;
    }
}

} // namespace helmsman::bridge
