// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fifth mission on the bridge: orders to Digifal, the Klingon cruiser
// Nizra hunting a "criminal" there, and after the landing party is back,
// Admiral Kenka with three heavy cruisers. Talking him down leads straight
// on to the sixth mission; defying him means a battle that cannot be won.
#include "bridge/missions.hpp"

namespace helmsman::bridge {

using namespace script;

namespace {

enum Stage : int {
    kStart = 0,    // the flight to Digifal and the Nizra
    kInOrbit = 1,  // in orbit, the landing party down; then Kenka
    kKenka = 2,    // the battle with Kenka's cruisers
};

enum Var : int {
    kFighting = 0, // the Nizra has turned on the Enterprise
    kArrived = 1,  // at Digifal; the Nizra is on its way
    kDelay = 2,    // ticks until the Nizra comes
};

const char* kTaraz = "Taraz";
const char* kKenkaName = "Admiral Kenka";

void taraz(Bridge& b, int line) { b.sayAs(kTaraz, b.text(line), 0xA0, 0xBE, 0x2C); }
void kenka(Bridge& b, int line) { b.sayAs(kKenkaName, b.text(line), 0xA0, 0xBE, 0x2C); }

void armNizra(Bridge& b, bool on) {
    if (!present(b, 0)) {
        return;
    }
    auto& parts = ship(b, 0).cls.parts;
    parts[space::kPortTorpedoes].on = on;
    parts[space::kPortPhasers].on = on;
    parts[space::kStarboardPhasers].on = on;
}

void nizraAttacks(Bridge& b) {
    taraz(b, 9);
    b.hideComm();
    b.music(2);
    b.sound(0x28);
    spock(b, 10);
    b.voyage().vars[kFighting] = 1;
    armNizra(b, true);
}

// The Nizra comes and Commander Taraz speaks.
void nizra(Bridge& b) {
    b.space().addShip("klingon");
    armNizra(b, false);
    uhura(b, 6);
    b.sound(0x22);
    kirk(b, 7);
    b.showComm("klg1");
    taraz(b, 8);
    const int answer = b.choose({0, 1, 2});
    if (answer != 1) {
        nizraAttacks(b);
        return;
    }
    taraz(b, 11);
    if (b.choose({3, 4}) == 1) {
        nizraAttacks(b);
        return;
    }
    b.hideComm(); // the Nizra stays to watch
}

// Kenka's ultimatum: talked down, or a battle.
void kenkaTalk(Bridge& b) {
    Voyage& v = b.voyage();
    b.showComm("klg2");
    kenka(b, 23);
    bool defied = b.choose({14, 15, 16}) == 2;
    if (!defied) {
        kenka(b, 24);
        kirk(b, 25);
        kenka(b, 26);
        defied = b.choose({17, 18, 19}) == 2;
    }
    if (defied) {
        b.hideComm();
        sulu(b, 34);
        b.music(2);
        b.sound(0x28);
        b.space().addShip("K'LRTA");
        v.clock = 0;
        v.stage = kKenka;
        return;
    }
    kenka(b, 27);
    b.sound(0x22);
    uhura(b, 28);
    kirk(b, 29);
    kenka(b, 30);
    b.hideComm();
    b.comm("woman", "Admiral", b.text(31));
    scott(b, 32);
    kirk(b, 33);
    // On to Hrakkour, where the sixth mission begins.
    b.setFullView(true);
    b.setControl(false);
    if (present(b, 0)) {
        b.space().remove(space::Space::kFirstShip);
    }
    v.system = 8;
    v.destination = 8;
    b.setControl(true);
    b.setFullView(false);
    v.clock = 0;
    v.mission = kTrial;
    v.stage = 0;
    b.clearEvents();
}

// The Nizra gives up once its shields are down by a third.
void nizraFlees(Bridge& b) {
    if (!present(b, 0) || ship(b, 0).behaviour == 1) {
        return;
    }
    int now = 0;
    int best = 0;
    for (const space::Part& s : ship(b, 0).cls.shields) {
        now += s.now;
        best += s.best;
    }
    if (now * 3 < best * 2) {
        ship(b, 0).behaviour = 1;
        b.space().addShip("klingon");
    }
}

} // namespace

void featherEvent(Bridge& b, const Event& e) {
    Voyage& v = b.voyage();
    switch (v.stage) {
    case kStart:
        if (e.type == kTick) {
            if (v.clock == 1) {
                b.session().loadMusic("bridgeb");
                b.resetShip();
                v.textFile = "feather";
                v.vars = {};
                v.destination = 6;
                b.flyby("feather", "FEA0\\FLYBY", kStart);
            } else if (v.clock == 0x97) {
                kirk(b, 5);
            }
            if (v.vars[kFighting] == 1) {
                nizraFlees(b);
            }
            if (v.vars[kArrived] != 0 && v.vars[kFighting] == 0) {
                if (v.vars[kDelay] == 0) {
                    v.vars[kArrived] = 0;
                    nizra(b);
                } else {
                    v.vars[kDelay] -= 1;
                }
            }
            b.defaultEvent(e);
            return;
        }
        if (e.type == kOrder) {
            switch (e.what) {
            case kFirePhasers:
            case kFireTorpedoes:
                if (v.vars[kFighting] == 0 && v.underAttack && b.space().player().weaponsArmed) {
                    v.vars[kFighting] = 1;
                    b.comm("klg1", kTaraz, b.text(12));
                    armNizra(b, true);
                }
                break;
            case kHail:
                uhura(b, 13);
                return;
            case kOrbit:
                // The original only goes on when the Nizra stood down; after
                // a fight it waits until the Nizra is gone.
                if (v.system == v.destination && (v.vars[kFighting] == 0 || !v.underAttack)) {
                    v.clock = 0;
                    v.stage = kInOrbit;
                    return;
                }
                break;
            case kStarMap: {
                if (v.system == v.destination) {
                    if (v.vars[kFighting] == 0) {
                        v.underAttack = false;
                    }
                    b.defaultEvent(e);
                    return;
                }
                b.defaultEvent(e);
                if (v.system == v.destination) {
                    v.vars[kArrived] = 1;
                    v.vars[kDelay] = 0x1E;
                }
                return;
            }
            default:
                break;
            }
        }
        b.defaultEvent(e);
        return;

    case kInOrbit:
        v.underAttack = false; // the Nizra only watches now
        if (e.type == kTick && v.clock == 1) {
            b.orbit(true, true);
        } else if (e.type == kOrder && e.what == kSpockTalk) {
            spock(b, 20);
            return;
        } else if (e.type == kBeamedUp) {
            b.orbit(false, false);
            uhura(b, 21);
            b.sound(0x22);
            uhura(b, 22);
            b.post({kSaid, 1});
            return;
        } else if (e.type == kSaid && e.what == 1) {
            kenkaTalk(b);
            return;
        } else if (e.type == kOrder && e.what == kOrbit) {
            b.sayAt(kSulu, b.text(35), 0x7A, 0x74, 0x2C);
            return;
        }
        b.defaultEvent(e);
        return;

    case kKenka:
        // More cruisers come as long as there is room for them.
        b.space().addShip("klingon");
        b.defaultEvent(e);
        return;

    default:
        b.defaultEvent(e);
        return;
    }
}

} // namespace helmsman::bridge
