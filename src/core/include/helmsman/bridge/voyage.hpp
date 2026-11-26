// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the Enterprise is in the game as a whole: which mission it is on and
// how far, where it is on the star map, and what Starfleet thought of the
// missions done. Saved with the game.
#pragma once

#include "helmsman/engine/savegame.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace helmsman::bridge {

// The missions, in the order they are played.
enum MissionId : int {
    kDemon = 0,
    kTug = 1,
    kLove = 2,
    kMudd = 3,
    kFeather = 4,
    kTrial = 5,
    kSins = 6,
    kVeng = 7,
    kMissionCount = 8,
};

// The mission's name, as its files are named ("demon", "tug", ...).
const char* missionName(int mission);

struct Voyage {
    int mission = kDemon;
    int stage = 0;               // the step of the mission's script
    std::uint32_t clock = 0;     // ticks since the script last set it
    std::array<int, 8> vars{};   // the script's own notes
    int system = -1;             // the star system the ship is at (-1: none)
    int destination = 0x12;      // where Starfleet sends it
    bool inOrbit = false;
    bool underAttack = false;    // a fight the ship cannot leave
    bool busy = false;           // a scene runs; the player only watches
    int encounter = 0;           // a random encounter under way (its kind)
    int encounterStage = 0;
    std::string textFile;        // the mission's text (MISSION.TXT)

    // The flyby that opens a mission.
    bool flyby = false;
    std::string flybyTitle;
    std::string flybyVoice;
    int flybyStage = 0;

    // What the last planet mission reported: points, best possible, and
    // a number of its own.
    int awayScore = 0;
    int awayBest = 0;
    int awayExtra = 0;
    bool redshirtDead = false;

    // Starfleet's commendations, by mission (-1: not done yet).
    std::array<int, kMissionCount> commendations{-1, -1, -1, -1, -1, -1, -1, -1};
    int lastScored = -1;
    int rating = 0;
    int points = 0;

    // The planet of the system the ship is in: its number and colour ramp
    // (-1: none).
    int planetSeed = -1;
    int planetRamp = 0;

    void save(SaveWriter& w) const;
    void restore(SaveReader& r);
};

} // namespace helmsman::bridge
