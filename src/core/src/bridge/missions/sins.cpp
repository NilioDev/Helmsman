// SPDX-License-Identifier: GPL-3.0-or-later
//
// The seventh mission on the bridge: orders to Alpha Proxima and the
// asteroid Scythe, a scan from orbit, and Starfleet's verdict once the
// landing party is back.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,  // the orders and the flight to Alpha Proxima
    kAtScythe = 1,
};

enum Var : int {
    kScanned = 0, // Spock has scanned Scythe
};

constexpr int kScoreNumber = 5; // this mission's score, see Bridge::score

} // namespace

void sinsEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridge");
                b.resetShip();
                v.textFile = "sins";
                v.vars = {};
                b.flyby("devilmon", "SIN0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                b.sound(0x22);
                uhura(b, 0);
                kirk(b, 1);
                b.comm("woman", "Admiral", b.text(2));
                spock(b, 3);
                kirk(b, 4);
                spock(b, 5);
                chekov(b, 6);
                spock(b, 7);
                kirk(b, 8);
                spock(b, 9);
                v.destination = 0xB;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder) {
            switch (e.what) {
            case kHail:
                uhura(b, 10);
                return;
            case kSpockTalk:
                spock(b, 11);
                return;
            case kStarMap:
                b.defaultEvent(e);
                if (v.system == v.destination) {
                    v.clock = 0;
                    v.stage = kAtScythe;
                }
                return;
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kAtScythe:
        if (e.type == kOrder && e.what == kHail) {
            uhura(b, v.vars[kScanned] != 0 ? 12 : 13);
            return;
        }
        if (e.type == kOrder && e.what == kSpockTalk) {
            if (!v.inOrbit) {
                spock(b, 14);
            } else if (v.vars[kScanned] == 0) {
                v.vars[kScanned] = 1;
                spock(b, 15);
                sulu(b, 16);
                kirk(b, 17);
                uhura(b, 18);
                kirk(b, 19);
                spock(b, 20);
            } else {
                spock(b, 21);
            }
            return;
        }
        if (e.type == kBeamedUp) {
            v.busy = true;
            b.placeCrew(kSpock, "xstndsp");
            b.placeCrew(kMcCoy, "xstndmc");
            b.sound(0x22);
            uhura(b, 22);
            kirk(b, 23);
            b.score(kScoreNumber, 24, v.awayScore, v.awayBest);
            mccoy(b, 25);
            spock(b, 26);
            mccoy(b, 27);
            kirk(b, 28);
            b.sayAt(kKirk, b.text(29), 0xA0, 0x64, 0x2C);
            mccoy(b, 30);
            kirk(b, 31);
            b.orbit(false, false);
            b.placeCrew(kSpock, "bstndsp");
            b.removeCrew(kMcCoy);
            v.mission = kVeng;
            v.stage = 0;
            v.clock = 0;
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
