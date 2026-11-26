// SPDX-License-Identifier: GPL-3.0-or-later
//
// The missions' scripts on the bridge: what each mission does with the
// bridge's events, step by step. See docs/rules/missions.md.
#pragma once

#include "helmsman/bridge/bridge.hpp"

namespace helmsman::bridge {

// Gives the event to the mission under way.
void missionEvent(Bridge& b, const Event& e);

// Random encounters on the way to somewhere else.
void startEncounter(Bridge& b);
void encounterEvent(Bridge& b, const Event& e);

void demonEvent(Bridge& b, const Event& e);
void tugEvent(Bridge& b, const Event& e);
void loveEvent(Bridge& b, const Event& e);
void muddEvent(Bridge& b, const Event& e);
void featherEvent(Bridge& b, const Event& e);
void trialEvent(Bridge& b, const Event& e);
void sinsEvent(Bridge& b, const Event& e);
void vengEvent(Bridge& b, const Event& e);

// Small helpers for the scripts.
namespace script {

// Each officer's box, as the scripts place them.
inline int kirk(Bridge& b, int line, int number = 0) { return b.sayAt(kKirk, b.text(line), 0xA0, 0x82, 0xB0, number); }
inline int spock(Bridge& b, int line, int number = 0) { return b.sayAt(kSpock, b.text(line), 0x126, 0x6A, 0x2C, number); }
inline int mccoy(Bridge& b, int line, int number = 0) { return b.sayAt(kMcCoy, b.text(line), 0xA0, 0x64, 0x2C, number); }
inline int sulu(Bridge& b, int line, int number = 0) { return b.sayAt(kSulu, b.text(line), 0x7A, 0x74, 0xB0, number); }
inline int chekov(Bridge& b, int line, int number = 0) { return b.sayAt(kChekov, b.text(line), 0xC4, 0x74, 0xB0, number); }
inline int uhura(Bridge& b, int line, int number = 0) { return b.sayAt(kUhura, b.text(line), 0x12A, 0x96, 0xA1, number); }
inline int scott(Bridge& b, int line, int number = 0) { return b.sayAt(kScott, b.text(line), 0x40, 0x64, 0xA1, number); }

// A ship of the mission: its slot in space.
inline space::Object& ship(Bridge& b, int n) { return b.space().object(space::Space::kFirstShip + n); }
inline bool present(Bridge& b, int n) { return ship(b, n).kind == space::Kind::Ship; }
// True when the part has less than half its strength.
inline bool belowHalf(const space::Object& s, int part) {
    const space::Part& p = s.cls.parts[static_cast<std::size_t>(part)];
    return p.now < p.best / 2;
}

} // namespace script

} // namespace helmsman::bridge
