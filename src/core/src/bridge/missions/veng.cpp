// SPDX-License-Identifier: GPL-3.0-or-later
//
// The last mission on the bridge: the wreck of the U.S.S. Republic, where
// the landing party goes straight over; then the chase after the ship that
// destroyed it, Doctor Bredell, the battle, Starfleet's verdict on the
// mission and on the whole year, and the closing titles.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,  // at the Republic
    kBattle = 1, // the false Enterprise and its friends
};

enum Var : int {
    kAwayScore = 0, // the landing party's points, kept for the verdict
    kAwayBest = 1,
};

constexpr int kScoreNumber = 6; // this mission's score, see Bridge::score

void bredell(Bridge& b, int line) { b.sayAs("Bredell", b.text(line), 0xA0, 0xBE, 0x2C); }

// Doctor Bredell on the comm screen, and the battle that follows.
void bredellTalk(Bridge& b) {
    Voyage& v = b.voyage();
    b.showComm("maddoc");
    bredell(b, 21);
    kirk(b, 22);
    bredell(b, 23);
    switch (b.choose({4, 5, 6})) {
    case 0:
        bredell(b, 24);
        kirk(b, 25);
        bredell(b, 26);
        kirk(b, 27);
        bredell(b, 28);
        b.choose({7, 8, 9}); // all three get the same answer
        bredell(b, 29);
        break;
    case 1:
        bredell(b, 30);
        break;
    default:
        bredell(b, 31);
        break;
    }
    b.space().addShip("vardain");
    v.underAttack = true;
    b.hideComm();
    v.stage = kBattle;
    v.clock = 0;
    b.music(2);
    b.sound(0x28);
}

// The battle is won: the verdicts, then the closing titles.
void ending(Bridge& b) {
    Voyage& v = b.voyage();
    b.setFullView(false);
    b.setControl(true);
    v.busy = true;
    spock(b, 37);
    kirk(b, 38);
    scott(b, 39);
    chekov(b, 40);
    kirk(b, 41);
    uhura(b, 42);
    kirk(b, 43);
    b.score(kScoreNumber, 44, v.vars[kAwayScore], v.vars[kAwayBest]);
    const int percent = b.yearReview(45);
    auto admiral = [&b](int line) {
        b.sayAs("Admiral", b.text(line), 0xA0, 0xBE, 0x2C);
        b.hideComm();
    };
    if (percent <= 50) {
        admiral(46);
        mccoy(b, 47);
        spock(b, 48);
        kirk(b, 49);
        mccoy(b, 50);
        spock(b, 51);
        kirk(b, 52);
    } else if (percent <= 85) {
        admiral(53);
        kirk(b, 54);
        spock(b, 55);
        mccoy(b, 56);
        spock(b, 57);
        kirk(b, 58);
        mccoy(b, 59);
        kirk(b, 60);
    } else {
        admiral(61);
        mccoy(b, 62);
        spock(b, 63);
        mccoy(b, 64);
        kirk(b, 65);
        spock(b, 66);
        mccoy(b, 67);
        kirk(b, 68);
    }
    b.setFullView(true);
    v.busy = false;
    b.clearEvents();
    b.endGame();
}

} // namespace

void vengEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridgew");
                v.destination = v.system;
                b.resetShip();
                v.textFile = "veng";
                v.vars = {};
                b.flyby("vengence", "VEN0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                kirk(b, 10);
                spock(b, 11);
                b.approach(7);
                kirk(b, 12);
                uhura(b, 13);
                b.choose({0, 1}); // the packet goes either way
                v.inOrbit = true;
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
            v.vars[kAwayScore] = v.awayScore;
            v.vars[kAwayBest] = v.awayBest;
            v.inOrbit = false;
            b.setFullView(true);
            b.setControl(false);
            b.removeStill();
            b.setFullView(false);
            b.setControl(true);
            b.placeCrew(kMcCoy, "xstndmc");
            space::Object& p = b.space().player();
            p.wantSpeed = p.cls.maxSpeed;
            kirk(b, 14);
            spock(b, 15);
            kirk(b, 16);
            spock(b, 17);
            spock(b, 18);
            uhura(b, 19);
            spock(b, 20);
            if (b.choose({2, 3}) != 0) {
                sulu(b, 32);
                b.sound(0x22);
                uhura(b, 33);
            }
            bredellTalk(b);
        }
        return; // nothing else until the landing party is back

    case kBattle:
        if (e.type == kTick) {
            if (v.clock == 0x12C && v.underAttack) {
                // Bredell calls for help.
                b.sound(0x28);
                spock(b, 34);
                b.space().addShip("3PIRATE");
                b.space().addShip("orion");
            }
            if (!v.underAttack) {
                ending(b);
                return;
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder && e.what == kSpockTalk) {
            spock(b, 35);
            return;
        }
        if (e.type == kOrder && e.what == kHail) {
            uhura(b, 36);
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
