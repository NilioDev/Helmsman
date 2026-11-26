// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sixth mission on the bridge: in orbit of Hrakkour, the landing party
// goes straight down with Quetzecoatl. What the bridge hears afterwards
// depends on how the trial ended; one ending brings Kallarax of the
// K'lirta, who can be answered well or badly. Starfleet then judges the
// fifth and sixth missions together.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,        // in orbit of Hrakkour
    kKenkaGloats = 1,  // the trial was lost
    kMourning = 2,     // Quetzecoatl died
    kVlictLeft = 3,    // the Klingon ship leaves
    kKallarax = 4,     // Vlict is dead; Kallarax calls
    kKallaraxFights = 5,
};

enum Var : int {
    kFeatherScore = 1, // the fifth mission's landing party's points
    kFeatherBest = 2,
};

void kallarax(Bridge& b, int line) { b.sayAs("KALLARAX", b.text(line), 0xA0, 0xBE, 0x2C); }

// Warp away, Starfleet's verdict, and on to the seventh mission.
void finish(Bridge& b) {
    Voyage& v = b.voyage();
    b.setFullView(true);
    b.setControl(false);
    b.setFullView(false);
    b.setControl(true);
    b.score(kFeather, 26, v.vars[kFeatherScore] + v.awayScore, v.vars[kFeatherBest] + v.awayBest);
    v.clock = 0;
    v.mission = kSins;
    v.stage = 0;
    v.busy = false;
}

// The endings that only talk: at clock 0x14 the lines are said and the
// ship leaves. Gives true when the event was taken.
bool talkThenLeave(Bridge& b, const Event& e, void (*lines)(Bridge&)) {
    Voyage& v = b.voyage();
    if (e.type != kTick) {
        return true;
    }
    if (v.clock == 0x14) {
        v.busy = true;
        lines(b);
        finish(b);
        return true;
    }
    return false;
}

void kenkaGloats(Bridge& b) {
    b.sound(0x22);
    uhura(b, 1);
    b.comm("klg2", "Vlict Kenka", b.text(2));
    uhura(b, 3);
    kirk(b, 4);
}

void mourning(Bridge& b) {
    kirk(b, 5);
    spock(b, 6);
    kirk(b, 7);
    spock(b, 8);
    kirk(b, 9);
}

void vlictLeft(Bridge& b) {
    sulu(b, 10);
    kirk(b, 11);
    spock(b, 12);
    kirk(b, 13);
}

} // namespace

void trialEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                v.system = 8;
                v.destination = 8;
                if (v.planetSeed < 0) {
                    b.newPlanet(8); // Hrakkour, when the voyage starts here
                }
                v.textFile = "trial";
                v.vars = {};
                v.vars[kFeatherScore] = v.awayScore;
                v.vars[kFeatherBest] = v.awayBest;
                b.orbit(true, false);
            } else if (v.clock == 0x32) {
                kirk(b, 0);
                b.post({kOrder, kTransporter});
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder && e.what == kTransporter) {
            b.defaultEvent(e);
            return;
        }
        if (e.type == kBeamedUp) {
            b.orbit(false, false);
            v.clock = 0;
            switch (v.awayExtra) {
            case 0: v.stage = kKenkaGloats; break;
            case 1:
            case 2: v.stage = kMourning; break;
            case 3: v.stage = kVlictLeft; break;
            case 4: v.stage = kKallarax; break;
            default: break;
            }
        }
        return; // nothing else while the landing party is ready to go

    case kKenkaGloats:
        if (!talkThenLeave(b, e, kenkaGloats)) {
            b.defaultEvent(e);
        }
        return;
    case kMourning:
        if (!talkThenLeave(b, e, mourning)) {
            b.defaultEvent(e);
        }
        return;
    case kVlictLeft:
        if (!talkThenLeave(b, e, vlictLeft)) {
            b.defaultEvent(e);
        }
        return;

    case kKallarax:
        if (e.type != kTick) {
            return;
        }
        if (v.clock == 0x14) {
            v.busy = true;
            b.sound(0x22);
            uhura(b, 17);
            b.showComm("klg1");
            kallarax(b, 18);
            if (b.choose({14, 15, 16}) == 2) {
                // Mocking him: the comm screen stays up for his answer.
                v.stage = kKallaraxFights;
                v.clock = 0;
                return;
            }
            kallarax(b, 19);
            b.hideComm();
            uhura(b, 20);
            b.chooseWords({b.text(21), " Set course for Digifal, Mr. Sulu. Warp Factor Six."});
            finish(b);
            return;
        }
        b.defaultEvent(e);
        return;

    case kKallaraxFights:
        if (e.type == kTick) {
            if (v.clock == 1) {
                kallarax(b, 22);
                b.hideComm();
                b.music(2);
                b.sound(0x28);
                spock(b, 23);
                b.space().addShip("K'LRTA");
                v.underAttack = true;
                v.busy = false;
            } else if (v.clock > 2 && !v.underAttack) {
                v.busy = true;
                if (b.song() >= 0) {
                    b.music(-1);
                }
                kirk(b, 24);
                spock(b, 25);
                finish(b);
                return;
            }
        }
        b.defaultEvent(e);
        return;

    default:
        b.defaultEvent(e);
        return;
    }
}

} // namespace helmsman::bridge
