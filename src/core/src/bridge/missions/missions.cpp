// SPDX-License-Identifier: GPL-3.0-or-later
#include "bridge/missions.hpp"

namespace helmsman::bridge {

void missionEvent(Bridge& b, const Event& e) {
    switch (b.voyage().mission) {
    case kDemon: demonEvent(b, e); break;
    case kTug: tugEvent(b, e); break;
    case kLove: loveEvent(b, e); break;
    case kMudd: muddEvent(b, e); break;
    case kFeather: featherEvent(b, e); break;
    case kTrial: trialEvent(b, e); break;
    case kSins: sinsEvent(b, e); break;
    case kVeng: vengEvent(b, e); break;
    default: b.defaultEvent(e); break;
    }
}

// Not written yet: the bridge as it always is.
void featherEvent(Bridge& b, const Event& e) { b.defaultEvent(e); }
void trialEvent(Bridge& b, const Event& e) { b.defaultEvent(e); }
void sinsEvent(Bridge& b, const Event& e) { b.defaultEvent(e); }
void vengEvent(Bridge& b, const Event& e) { b.defaultEvent(e); }
void startEncounter(Bridge&) {}
void encounterEvent(Bridge& b, const Event& e) { b.defaultEvent(e); }

} // namespace helmsman::bridge
