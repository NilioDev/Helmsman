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

} // namespace helmsman::bridge
