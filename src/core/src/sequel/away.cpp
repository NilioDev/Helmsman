// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's away missions: rooms whose code (the room's .RDF, the
// mission's and DATA.RDF) answers events, people who walk and animate, and
// the orders the player gives. See docs/rules/sequel-rooms.md.
#include "game_impl.hpp"

#include <cmath>
#include <cstdlib>

namespace helmsman::sequel::detail {

namespace {

// The room file's header (offsets in its segment).
namespace room {
constexpr std::uint16_t kMaxY = 0x08;
constexpr std::uint16_t kMinY = 0x0A;
constexpr std::uint16_t kScaleNear = 0x0E; // at the bottom
constexpr std::uint16_t kScaleFar = 0x0C;  // at the top
constexpr std::uint16_t kLists = 0x10;
constexpr std::uint16_t kHotspots = 0x12; // first, end
constexpr std::uint16_t kExits = 0x16;
constexpr std::uint16_t kAreas = 0x1A;
constexpr std::uint16_t kBans = 0x1E;
constexpr std::uint16_t kWayIn = 0x22;
constexpr std::uint16_t kExitRooms = 0x26;
} // namespace room

// The mission's and DATA.RDF's header.
namespace mission {
constexpr std::uint16_t kLists = 0x08;
constexpr std::uint16_t kItems = 0x0A; // first, end of the item names
constexpr std::uint16_t kFirstStranger = 0x0E;
} // namespace mission

// Places in a person's record.
namespace rec {
constexpr std::uint16_t kFlags = 0x00; // bit 0 in use, 3 shown, 4 done
constexpr std::uint16_t kX = 0x0C;
constexpr std::uint16_t kY = 0x0E;
constexpr std::uint16_t kFollow = 0x18;
constexpr std::uint16_t kCode = 0x44;
constexpr std::uint16_t kTeam = 0x45;
constexpr std::uint16_t kHeld = 0x46;
constexpr std::uint16_t kLetter = 0x48;
constexpr std::uint16_t kFacing = 0x49;
constexpr std::uint16_t kNextFacing = 0x4A;
constexpr std::uint16_t kAnim = 0x6A;
} // namespace rec

namespace flag {
constexpr std::uint8_t kInUse = 0x01;
constexpr std::uint8_t kShown = 0x08;
constexpr std::uint8_t kDone = 0x10;
} // namespace flag

namespace team {
constexpr std::uint8_t kBeamed = 0x01;
constexpr std::uint8_t kHeld = 0x02;
constexpr std::uint8_t kOnTeam = 0x04;
constexpr std::uint8_t kShown = 0x08;
constexpr std::uint8_t kFollows = 0x10;
constexpr std::uint8_t kScaled = 0x20;
} // namespace team

// Places in the data segment.
namespace spot {
constexpr std::uint16_t kItems = 0x0F5A;    // 10 bytes an item: have, name
constexpr std::uint16_t kHandlers = 0x526A; // {kind, table, priority} x 7
constexpr std::uint16_t kHandlerCount = 0x5294;
constexpr std::uint16_t kBusy = 0xC217;
constexpr std::uint16_t kMouseX = 0xC212;
constexpr std::uint16_t kMouseY = 0xC214;
constexpr std::uint16_t kQuiet = 0xC21E;
constexpr std::uint16_t kNotHandled = 0xC21F;
constexpr std::uint16_t kResultA = 0xC222;
constexpr std::uint16_t kResultB = 0xC224;
constexpr std::uint16_t kTimers = 0xC202; // 8 words
} // namespace spot

// Events.
namespace ev {
constexpr std::uint8_t kTick = 0x00;
constexpr std::uint8_t kWalk = 0x01;
constexpr std::uint8_t kUse = 0x02;
constexpr std::uint8_t kGet = 0x03;
constexpr std::uint8_t kLook = 0x04;
constexpr std::uint8_t kTalk = 0x05;
constexpr std::uint8_t kExit = 0x06;
constexpr std::uint8_t kEnterArea = 0x07;
constexpr std::uint8_t kInArea = 0x08;
constexpr std::uint8_t kTimer = 0x09;
constexpr std::uint8_t kDone = 0x0A;
constexpr std::uint8_t kMissionBegins = 0x0B;
constexpr std::uint8_t kRestored = 0x0C;
constexpr std::uint8_t kStopInArea = 0x0D;
constexpr std::uint8_t kLeaveArea = 0x0E;
} // namespace ev

constexpr std::uint8_t kArrived = 0xFF; // the code of Kirk's way in
constexpr int kKirk = 0;
constexpr int kCrewSlots = 9; // people 9 and up are taken for strangers

std::int16_t s16(std::uint16_t v) { return static_cast<std::int16_t>(v); }

int roundFixed(std::int32_t v) { return (v + 0x8000) >> 16; }

// What each action picks up from the action figure (ACTION.BMP, 54 x 44):
// the parts of the body.
struct ActionArea {
    int action;
    int x0, y0, x1, y1;
};
constexpr int kOptions = 6;
constexpr ActionArea kActionAreas[] = {
    {kOptions, 38, 2, 52, 17}, // the insignia
    {4, 18, 6, 35, 13},        // the eyes: look
    {5, 18, 13, 35, 19},       // the mouth: talk
    {2, 2, 16, 15, 30},        // the hand with the sphere: use
    {3, 40, 32, 53, 43},       // the open hand: get
    {1, 14, 19, 40, 44},       // the body: walk
};

const char* cursorFor(int action, bool lit) {
    switch (action) {
    case 2:
        return lit ? "useh" : "usen";
    case 3:
        return lit ? "geth" : "getn";
    case 4:
        return lit ? "lookh0" : "lookn";
    case 5:
        return lit ? "talkh0" : "talkn";
    default:
        return "walk";
    }
}

} // namespace

bool Polygon::contains(int x, int y) const {
    // Even-odd rule over the edges.
    bool in = false;
    const std::size_t n = points.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto [xi, yi] = points[i];
        const auto [xj, yj] = points[j];
        if ((yi > y) != (yj > y)) {
            const long long cross = static_cast<long long>(xj - xi) * (y - yi);
            const int at = xi + static_cast<int>(cross / (yj - yi));
            if (x < at) {
                in = !in;
            }
        }
    }
    return in;
}

// --- Files and pictures ---------------------------------------------------

std::shared_ptr<const Bitmap> Game::scaledPicture(const std::string& name, int scale) {
    if (scale == 256) {
        return picture(name);
    }
    const auto key = std::make_pair(upperName(name), scale);
    const auto it = scaled_.find(key);
    if (it != scaled_.end()) {
        return it->second;
    }
    std::shared_ptr<const Bitmap> out;
    if (const auto pic = picture(name)) {
        out = std::make_shared<const Bitmap>(away::scaleBitmap(*pic, scale));
    }
    scaled_[key] = out;
    return out;
}

std::string Game::roomText(std::uint16_t off, std::size_t max) const { return m_.scriptText(currentSeg_, off, max); }

std::vector<Polygon> Game::polygons(std::uint16_t from, std::uint16_t to) const {
    std::vector<Polygon> out;
    const auto seg = Machine::kRoom;
    std::uint16_t at = m_.word(seg, from);
    const std::uint16_t end = m_.word(seg, to);
    while (at < end && out.size() < 64) {
        Polygon p;
        p.id = s16(m_.word(seg, at));
        const int n = std::clamp<int>(s16(m_.word(seg, static_cast<std::uint16_t>(at + 2))), 0, 64);
        for (int i = 0; i < n; ++i) {
            const auto o = static_cast<std::uint16_t>(at + 4 + 4 * i);
            p.points.emplace_back(s16(m_.word(seg, o)), s16(m_.word(seg, static_cast<std::uint16_t>(o + 2))));
        }
        at = static_cast<std::uint16_t>(at + 4 + 4 * n);
        if (n >= 3) {
            out.push_back(std::move(p));
        }
    }
    return out;
}

// --- Walking -------------------------------------------------------------

void Game::loadWalkMap(const std::string& name) {
    walkMap_.clear();
    nodes_.clear();
    nextHop_.assign(32 * 32, 0);
    if (auto map = file(name + ".MAP")) {
        walkMap_ = std::move(*map);
    }
    if (const auto iw = file(name + ".IW")) {
        Reader r(*iw);
        try {
            const int count = std::min<int>(r.u16(), 32);
            for (int i = 0; i < 32; ++i) {
                const int x = r.s16();
                const int y = r.s16();
                if (i < count) {
                    nodes_.emplace_back(x, y);
                }
            }
            for (int i = 0; i < count; ++i) {
                for (int j = 0; j < count; ++j) {
                    nextHop_[static_cast<std::size_t>(i * 32 + j)] = r.u8();
                }
            }
        } catch (const FormatError&) {
            // A short file: keep what was read.
        }
    }
}

bool Game::blocked(int x, int y) const {
    if (walkMap_.empty()) {
        return false;
    }
    if (x < 0 || y < 0 || x >= 320 || y >= 200) {
        return true;
    }
    const auto i = static_cast<std::size_t>(y * 40 + x / 8);
    if (i >= walkMap_.size()) {
        return true;
    }
    return (walkMap_[i] & (0x80 >> (x & 7))) != 0;
}

bool Game::lineClear(int x0, int y0, int x1, int y1) const {
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= n; ++i) {
        const int x = n == 0 ? x0 : x0 + (x1 - x0) * i / n;
        const int y = n == 0 ? y0 : y0 + (y1 - y0) * i / n;
        if (blocked(x, y)) {
            return false;
        }
    }
    return true;
}

int Game::nearestNode(int x, int y) const {
    int best = -1;
    long long bestD = 0;
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        const auto [nx, ny] = nodes_[static_cast<std::size_t>(i)];
        const long long d = static_cast<long long>(nx - x) * (nx - x) + static_cast<long long>(ny - y) * (ny - y);
        if ((best < 0 || d < bestD) && lineClear(x, y, nx, ny)) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

int Game::scaleAt(int y) const {
    const int maxY = s16(m_.word(Machine::kRoom, room::kMaxY));
    const int minY = s16(m_.word(Machine::kRoom, room::kMinY));
    const int far = s16(m_.word(Machine::kRoom, room::kScaleFar));
    const int nearScale = s16(m_.word(Machine::kRoom, room::kScaleNear));
    if (maxY < minY) {
        return 256;
    }
    y = std::clamp(y, minY, maxY);
    return std::clamp(far + (nearScale - far) * (y - minY) / (maxY - minY + 1), 16, 1024);
}

void Game::placeSprite(Body& b) {
    if (b.frames == nullptr || b.frames->empty()) {
        return;
    }
    const auto& f = (*b.frames)[std::min(b.frame, b.frames->size() - 1)];
    b.sprite.bitmap = scaledPicture(f.picture, b.scale);
    b.sprite.x = b.x + f.x * b.scale / 256;
    b.sprite.y = b.y + f.y * b.scale / 256;
    b.sprite.depth = std::clamp(depthOfRow(b.y + f.y) + f.depth, 0, 15);
}

int Game::startAnim(int who, std::string name, int x, int y, int scale) {
    if (who == -1) {
        for (int i = 31; i >= kCrewSlots; --i) {
            if ((rec8(i, rec::kFlags) & flag::kInUse) == 0) {
                who = i;
                break;
            }
        }
    }
    if (who < 0 || who >= 32) {
        return -1;
    }
    const auto r = record(who);
    if (x == -1) {
        x = m_.s16(static_cast<std::uint16_t>(r + rec::kX));
    }
    if (y == -1) {
        y = m_.s16(static_cast<std::uint16_t>(r + rec::kY));
    }
    if (scale == -1) {
        scale = (rec8(who, rec::kTeam) & team::kScaled) != 0 ? scaleAt(y) : 256;
    }
    // '?' at the start stands for the person's letter, at the end for the
    // way they face.
    if (!name.empty() && name.front() == '?' && rec8(who, rec::kLetter) != 0) {
        name.front() = static_cast<char>(rec8(who, rec::kLetter));
    }
    if (!name.empty() && name.back() == '?') {
        if (rec8(who, rec::kNextFacing) != 0) {
            setRec8(who, rec::kFacing, rec8(who, rec::kNextFacing));
            setRec8(who, rec::kNextFacing, 0);
        } else if (rec8(who, rec::kFacing) == 0) {
            setRec8(who, rec::kFacing, 's');
        }
        name.back() = static_cast<char>(rec8(who, rec::kFacing));
    }
    name = lowerName(name);
    m_.setText(static_cast<std::uint16_t>(r + rec::kAnim), name, 9);
    m_.set8(r, static_cast<std::uint8_t>((m_.u8(r) | flag::kInUse) & 0xB9 & ~flag::kDone));
    m_.set16(static_cast<std::uint16_t>(r + rec::kX), static_cast<std::uint16_t>(x));
    m_.set16(static_cast<std::uint16_t>(r + rec::kY), static_cast<std::uint16_t>(y));
    setRec8(who, rec::kCode, 0);

    Body& b = bodies_[who];
    b.used = true;
    b.walking = false;
    b.path.clear();
    b.anim = name;
    b.frames = &anim(name);
    b.frame = 0;
    b.x = x;
    b.y = y;
    b.scale = scale;
    b.held = false;
    b.code = 0;
    b.sprite.mode = away::SpriteMode::Normal;
    b.sprite.order = who;
    if (b.frames->empty()) {
        b.sprite.bitmap.reset();
        b.nextAt = awayTicks_ + 1;
        return who;
    }
    b.nextAt = awayTicks_ + static_cast<std::uint64_t>(std::max(1, b.frames->front().ticks));
    placeSprite(b);
    return who;
}

void Game::stand(int who) {
    if (who < 0 || who >= 32) {
        return;
    }
    if (rec8(who, rec::kLetter) == 0) {
        // Strangers have no standing pictures: they keep their last frame.
        bodies_[who].walking = false;
        bodies_[who].held = true;
        return;
    }
    startAnim(who, "?stnd?", -1, -1, -1);
}

void Game::finish(int who) {
    Body& b = bodies_[who];
    const auto r = record(who);
    m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) | flag::kDone));
    const std::uint8_t code = rec8(who, rec::kCode);
    if (code != 0) {
        setRec8(who, rec::kCode, 0);
        post(ev::kDone, code);
    }
    b.code = 0;
}

void Game::setWalkPicture(Body& b, int who) {
    b.scale = (rec8(who, rec::kTeam) & team::kScaled) != 0 ? scaleAt(b.y) : 256;
    char name[16];
    std::snprintf(name, sizeof name, "%cwalk%c%02d", static_cast<char>(rec8(who, rec::kLetter)), b.dir, b.walkFrame & 7);
    b.sprite.bitmap = scaledPicture(name, b.scale);
    b.sprite.x = b.x;
    b.sprite.y = b.y;
    b.sprite.depth = std::clamp(depthOfRow(b.y), 0, 15);
    b.sprite.mode = away::SpriteMode::Normal;
}

void Game::nextLeg(Body& b) {
    const auto [tx, ty] = b.path.front();
    b.path.pop_front();
    b.fx = b.x << 16;
    b.fy = b.y << 16;
    const int dx = tx - b.x;
    const int dy = ty - b.y;
    b.steps = std::max(std::abs(dx), std::abs(dy));
    if (b.steps == 0) {
        b.vx = 0;
        b.vy = 0;
        return;
    }
    b.vx = static_cast<std::int32_t>((static_cast<std::int64_t>(dx) << 16) / b.steps);
    b.vy = static_cast<std::int32_t>((static_cast<std::int64_t>(dy) << 16) / b.steps);
    if (std::abs(dx) > std::abs(dy)) {
        b.dir = dx > 0 ? 'e' : 'w';
    } else {
        b.dir = dy > 0 ? 's' : 'n';
    }
}

bool Game::walk(int who, int fx, int fy, int tx, int ty, int code) {
    if (who < 0 || who >= 32) {
        return false;
    }
    const auto r = record(who);
    if (fx == -1) {
        fx = m_.s16(static_cast<std::uint16_t>(r + rec::kX));
    }
    if (fy == -1) {
        fy = m_.s16(static_cast<std::uint16_t>(r + rec::kY));
    }
    Body& b = bodies_[who];
    if (rec8(who, rec::kLetter) == 0) {
        // Only the crew has walking pictures: others are put there.
        m_.set16(static_cast<std::uint16_t>(r + rec::kX), static_cast<std::uint16_t>(tx));
        m_.set16(static_cast<std::uint16_t>(r + rec::kY), static_cast<std::uint16_t>(ty));
        b.x = tx;
        b.y = ty;
        placeSprite(b);
        setRec8(who, rec::kCode, static_cast<std::uint8_t>(code));
        finish(who);
        return true;
    }
    b.used = true;
    m_.set8(r, static_cast<std::uint8_t>((m_.u8(r) | flag::kInUse) & ~flag::kDone));
    b.walking = true;
    b.held = false;
    b.frames = nullptr;
    b.x = fx;
    b.y = fy;
    b.walkFrame = 0;
    b.path.clear();
    setRec8(who, rec::kCode, static_cast<std::uint8_t>(code));
    if (!lineClear(fx, fy, tx, ty) && !nodes_.empty()) {
        int from = nearestNode(fx, fy);
        const int to = nearestNode(tx, ty);
        for (int guard = 0; from >= 0 && to >= 0 && guard < 32; ++guard) {
            b.path.push_back(nodes_[static_cast<std::size_t>(from)]);
            if (from == to) {
                break;
            }
            from = nextHop_[static_cast<std::size_t>((from & 31) * 32 + (to & 31))];
            if (from >= static_cast<int>(nodes_.size())) {
                break;
            }
        }
    }
    b.path.emplace_back(tx, ty);
    nextLeg(b);
    setWalkPicture(b, who);
    return true;
}

void Game::walkStep(Body& b, int who) {
    const int perTick = std::clamp((b.scale + 0x40) / 0x80, 1, 3);
    for (int k = 0; k < perTick; ++k) {
        while (b.steps == 0 && !b.path.empty()) {
            nextLeg(b);
        }
        if (b.steps == 0) {
            // There: stand facing the way of the last step.
            b.walking = false;
            setRec8(who, rec::kFacing, static_cast<std::uint8_t>(b.dir));
            const std::uint8_t code = rec8(who, rec::kCode);
            stand(who);
            setRec8(who, rec::kCode, code);
            finish(who);
            if (who == kKirk) {
                checkAreas(b.x, b.y);
                for (const auto& p : polygons(room::kAreas, room::kAreas + 2)) {
                    if (p.contains(b.x, b.y)) {
                        post(ev::kStopInArea, static_cast<std::uint8_t>(p.id));
                        break;
                    }
                }
            }
            return;
        }
        --b.steps;
        b.fx += b.vx;
        b.fy += b.vy;
        b.x = roundFixed(b.fx);
        b.y = roundFixed(b.fy);
        if ((b.steps & 1) == 0) {
            ++b.walkFrame;
        }
    }
    const auto r = record(who);
    m_.set16(static_cast<std::uint16_t>(r + rec::kX), static_cast<std::uint16_t>(b.x));
    m_.set16(static_cast<std::uint16_t>(r + rec::kY), static_cast<std::uint16_t>(b.y));
    setWalkPicture(b, who);
    if (who == kKirk) {
        checkAreas(b.x, b.y);
    }
}

// Kirk's place against the room's touch areas and ways out.
void Game::checkAreas(int x, int y) {
    int inArea = -1;
    for (const auto& p : polygons(room::kAreas, room::kAreas + 2)) {
        if (p.contains(x, y)) {
            inArea = p.id;
            break;
        }
    }
    if (inArea != area_) {
        if (area_ != -1) {
            post(ev::kLeaveArea, static_cast<std::uint8_t>(area_));
        }
        if (inArea != -1) {
            post(ev::kEnterArea, static_cast<std::uint8_t>(inArea));
        }
        area_ = inArea;
    } else if (inArea != -1) {
        post(ev::kInArea, static_cast<std::uint8_t>(inArea));
    }
    if (busy_) {
        return;
    }
    int exit = -1;
    for (const auto& p : polygons(room::kExits, room::kExits + 2)) {
        if (p.contains(x, y)) {
            exit = p.id;
            break;
        }
    }
    if (exit != -1 && exit != exitArmed_) {
        post(ev::kExit, static_cast<std::uint8_t>(exit));
    }
    exitArmed_ = exit;
}

// --- Time -----------------------------------------------------------------

void Game::advanceBodies() {
    for (int i = 0; i < 32; ++i) {
        Body& b = bodies_[i];
        if (!b.used) {
            continue;
        }
        if (b.walking) {
            walkStep(b, i);
            continue;
        }
        if (b.held || b.frames == nullptr || b.frames->empty() || awayTicks_ < b.nextAt) {
            continue;
        }
        const auto& frames = *b.frames;
        const std::size_t count = frames.size();
        if (b.anim.compare(0, 3, "tlk") == 0 && !(i == talker_ && speaking_)) {
            // A talking head keeps still unless it speaks.
            b.frame = 0;
            b.nextAt = awayTicks_ + 1;
            placeSprite(b);
            continue;
        }
        std::size_t next = frames[std::min(b.frame, count - 1)].next[rng_() % 4];
        if (next >= count) {
            next = count - 1;
        }
        const bool wasLast = b.frame == count - 1;
        b.frame = next;
        if (b.frame == count - 1 && !wasLast) {
            finish(i);
        } else if (count == 1 && (rec8(i, rec::kFlags) & flag::kDone) == 0) {
            finish(i);
        }
        b.nextAt = awayTicks_ + static_cast<std::uint64_t>(std::max(1, frames[b.frame].ticks));
        placeSprite(b);
    }
}

void Game::awayFrame() {
    drawAway();
    present();
}

void Game::waitAwayTicks(int ticks) {
    const std::uint64_t end = awayTicks_ + static_cast<std::uint64_t>(std::max(0, ticks));
    while (!p_.quit && awayTicks_ < end) {
        awayFrame();
    }
}

void Game::waitDone(int who) {
    if (who < 0 || who >= 32) {
        return;
    }
    for (int guard = 0; !p_.quit && guard < 18 * 60; ++guard) {
        if ((rec8(who, rec::kFlags) & flag::kDone) != 0 || !bodies_[who].used) {
            return;
        }
        awayFrame();
    }
}

// One turn of the mission's main loop: the clock, the timers, the queue.
void Game::awayTick() {
    // The scripts' eight timers count towards 0 and say when they get there.
    for (int i = 0; i < 8; ++i) {
        const auto at = static_cast<std::uint16_t>(spot::kTimers + 2 * i);
        const int v = m_.s16(at);
        if (v > 0) {
            m_.set16(at, static_cast<std::uint16_t>(v - 1));
            if (v == 1) {
                post(ev::kTimer, static_cast<std::uint8_t>(i));
            }
        } else if (v < 0) {
            m_.set16(at, static_cast<std::uint16_t>(v + 1));
            if (v == -1) {
                post(ev::kTimer, static_cast<std::uint8_t>(i));
            }
        }
    }
    ++roomTicks_;
    post(ev::kTick, static_cast<std::uint8_t>(roomTicks_), static_cast<std::uint8_t>(roomTicks_ >> 8));
    dispatchQueued();
}

// --- Events ----------------------------------------------------------------

void Game::post(std::uint8_t type, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    if (queue_.size() >= 16) {
        queue_.pop_front();
    }
    queue_.push_back({type, a, b, c});
}

std::uint16_t Game::handlerSeg(int kind) const {
    switch (kind) {
    case 0:
        return Machine::kRoom;
    case 1:
        return Machine::kShared;
    default:
        return Machine::kGlobal;
    }
}

void Game::popHandlers(int kind, int table) {
    const int count = std::clamp<int>(m_.s16(spot::kHandlerCount), 0, 7);
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        const auto at = static_cast<std::uint16_t>(spot::kHandlers + 6 * i);
        const int k = m_.s16(at);
        const int t = m_.u16(static_cast<std::uint16_t>(at + 2));
        if (k == kind && (table == -1 || t == table)) {
            continue;
        }
        const auto to = static_cast<std::uint16_t>(spot::kHandlers + 6 * kept);
        for (int w = 0; w < 6; w += 2) {
            m_.set16(static_cast<std::uint16_t>(to + w), m_.u16(static_cast<std::uint16_t>(at + w)));
        }
        ++kept;
    }
    m_.set16(spot::kHandlerCount, static_cast<std::uint16_t>(kept));
}

void Game::pushHandlers(int kind, std::uint16_t table, int priority) {
    popHandlers(kind, table);
    int count = std::clamp<int>(m_.s16(spot::kHandlerCount), 0, 7);
    if (count >= 7) {
        return;
    }
    // Kept in rising priority: the new one goes after those below it.
    int at = count;
    while (at > 0 && m_.s16(static_cast<std::uint16_t>(spot::kHandlers + 6 * (at - 1) + 4)) >= priority) {
        for (int w = 0; w < 6; w += 2) {
            m_.set16(static_cast<std::uint16_t>(spot::kHandlers + 6 * at + w),
                     m_.u16(static_cast<std::uint16_t>(spot::kHandlers + 6 * (at - 1) + w)));
        }
        --at;
    }
    const auto to = static_cast<std::uint16_t>(spot::kHandlers + 6 * at);
    m_.set16(to, static_cast<std::uint16_t>(kind));
    m_.set16(static_cast<std::uint16_t>(to + 2), table);
    m_.set16(static_cast<std::uint16_t>(to + 4), static_cast<std::uint16_t>(priority));
    m_.set16(spot::kHandlerCount, static_cast<std::uint16_t>(++count));
}

// Looks for handlers of `event` in one of the lists of every table. Without
// `run` it only says whether there is one.
bool Game::runList(int list, const std::uint8_t e[4], bool run) {
    const bool wild = !(e[0] == ev::kTick || e[0] == ev::kDone || e[0] == ev::kMissionBegins || e[0] == ev::kTimer);
    bool result = false;
    const int count = std::clamp<int>(m_.s16(spot::kHandlerCount), 0, 7);
    for (int i = 0; i < count; ++i) {
        const auto entry = static_cast<std::uint16_t>(spot::kHandlers + 6 * i);
        const int kind = m_.s16(entry);
        const std::uint16_t table = m_.u16(static_cast<std::uint16_t>(entry + 2));
        const std::uint16_t seg = handlerSeg(kind);
        std::uint16_t at = m_.word(seg, static_cast<std::uint16_t>(table + 4 * list));
        const std::uint16_t end = m_.word(seg, static_cast<std::uint16_t>(table + 4 * list + 2));
        bool nextTable = false;
        for (int guard = 0; at < end && guard < 4096; ++guard) {
            std::uint8_t h[4];
            for (int k = 0; k < 4; ++k) {
                h[k] = m_.cpu.read8(seg, static_cast<std::uint16_t>(at + k));
            }
            bool match = false;
            if (wild) {
                match = h[0] == e[0] && h[3] == e[3] && (h[1] == 0xFF || h[1] == e[1]) && (h[2] == 0xFF || h[2] == e[2]);
            } else {
                match = h[0] == e[0] && h[1] == e[1] && h[2] == e[2] && h[3] == e[3];
            }
            bool handled = false;
            if (match) {
                if (!run) {
                    handled = true;
                } else {
                    m_.set8(spot::kNotHandled, 0);
                    const auto savedSeg = currentSeg_;
                    const int savedKind = currentKind_;
                    currentSeg_ = seg;
                    currentKind_ = kind;
                    struct Back {
                        Game& g;
                        std::uint16_t seg;
                        int kind;
                        ~Back() {
                            g.currentSeg_ = seg;
                            g.currentKind_ = kind;
                        }
                    } back{*this, savedSeg, savedKind};
                    try {
                        m_.runHandler(seg, static_cast<std::uint16_t>(at + 6), e,
                                      [this](ScriptCall& c) { return roomCommand(c); });
                    } catch (const CpuError& err) {
                        std::fprintf(stderr, "room code at %04x:%04x stopped: %s\n", seg, at + 6, err.what());
                    }
                    handled = m_.u8(spot::kNotHandled) == 0;
                }
            }
            if (handled) {
                if (wild || e[0] != ev::kTick || list != 1) {
                    return true;
                }
                result = true;
                nextTable = true;
                break;
            }
            at = m_.word(seg, static_cast<std::uint16_t>(at + 4));
        }
        (void)nextTable;
    }
    return result;
}

// Offers an event to the lists in turn, as the original: list 0 first,
// then 1, then 2 and 3 for an event of the player's (`quiet` false means
// it comes from a script and `noFallback` keeps lists 2 and 3 out).
bool Game::sendEvent(bool fromScript, bool noFallback, const std::uint8_t e[4]) {
    const bool full = !fromScript && !noFallback;
    const std::uint8_t saved = m_.u8(spot::kQuiet);
    struct Back {
        Machine& m;
        std::uint8_t v;
        ~Back() { m.set8(spot::kQuiet, v); }
    } back{m_, saved};
    m_.set8(spot::kQuiet, full ? 0 : 1);
    if (runList(0, e, true)) {
        return full;
    }
    m_.set8(spot::kQuiet, 0);
    if (runList(1, e, !fromScript)) {
        return true;
    }
    m_.set8(spot::kQuiet, full ? 0 : 1);
    if (!noFallback && runList(2, e, true)) {
        return true;
    }
    return full && runList(3, e, true);
}

void Game::dispatchQueued() {
    for (int guard = 0; !queue_.empty() && guard < 64 && !p_.quit; ++guard) {
        const auto e = queue_.front();
        queue_.pop_front();
        if (p_.devTrace && e[0] != ev::kTick) {
            std::fprintf(stderr, "[%llu] event %02x %02x %02x\n", static_cast<unsigned long long>(awayTicks_), e[0], e[1], e[2]);
        }
        if (e[0] == ev::kDone && e[1] == kArrived) {
            busy_ = false;
            m_.set8(spot::kBusy, 0);
        }
        if (sendEvent(false, false, e.data())) {
            continue;
        }
        if (e[0] == ev::kWalk && !busy_) {
            walk(kKirk, -1, -1, p_.mouseX, p_.mouseY, 0);
        } else if (e[0] == ev::kExit) {
            const int to = s16(m_.word(Machine::kRoom, static_cast<std::uint16_t>(room::kExitRooms + 2 * e[1])));
            if (to >= 0) {
                throw ChangeRoom{to, e[1] ^ 1};
            }
        }
    }
}

// --- Rooms -----------------------------------------------------------------

void Game::loadRoom(int number, bool first) {
    const std::string mission = m_.text(ds::kEpisodeName, 16);
    const std::string name = mission + std::to_string(number);
    roomNumber_ = number;
    // The room's picture, depths and palette.
    const auto bgd = file(name + ".BGD");
    if (!bgd || bgd->size() < 768 + 64000) {
        throw FormatError("room picture " + name + " is missing");
    }
    // The colours: the common palette (the people), with the room's own
    // in the places rooms may use.
    std::vector<std::uint8_t> colours(768, 0);
    if (const auto common = file("PALETTE.PAL"); common && common->size() >= 768) {
        std::copy_n(common->begin(), 768, colours.begin());
    }
    std::fill_n(colours.begin(), 0x1E, 0);
    for (const auto [from, to] : {std::pair{0x1E, 0x180}, std::pair{0x270, 0x294}, std::pair{0x2A0, 0x2E2}}) {
        std::copy(bgd->begin() + from, bgd->begin() + to, colours.begin() + from);
    }
    roomPalette_ = Palette::fromBytes(colours);
    scene_.backdrop = Surface(320, 200);
    std::copy_n(bgd->begin() + 768, 64000, scene_.backdrop.data());
    if (const auto pri = file(name + ".PRI")) {
        scene_.setDepths(*pri);
    } else {
        std::fill(scene_.depths.begin(), scene_.depths.end(), 0);
    }
    shade_ = shadeTable(roomPalette_, 45);
    scaled_.clear();

    if (first) {
        // The code of every mission (DATA.RDF), then this mission's.
        m_.set16(spot::kHandlerCount, 0);
        const auto data = file("DATA.RDF");
        if (!data) {
            throw FormatError("DATA.RDF is missing");
        }
        m_.loadRoomCode(Machine::kGlobal, *data, false);
        pushHandlers(2, m_.word(Machine::kGlobal, mission::kLists), 0x7FFF);
        const auto code = file(mission + ".RDF");
        if (!code) {
            throw FormatError("the mission's code is missing");
        }
        m_.loadRoomCode(Machine::kShared, *code, false);
        pushHandlers(1, m_.word(Machine::kShared, mission::kLists), 1000);
        // The names of the things the mission brings, after DATA.RDF's.
        int item = 0x46;
        for (const auto seg : {Machine::kGlobal, Machine::kShared}) {
            std::uint16_t at = m_.word(seg, mission::kItems);
            const std::uint16_t end = m_.word(seg, static_cast<std::uint16_t>(mission::kItems + 2));
            while (at < end && item < 0x80) {
                const std::string n = m_.scriptText(seg, at, 32);
                const auto slot = static_cast<std::uint16_t>(spot::kItems + 10 * item);
                m_.setText(static_cast<std::uint16_t>(slot + 1), n.substr(0, 8), 9);
                m_.set8(slot, 1);
                at = static_cast<std::uint16_t>(at + n.size() + 1);
                ++item;
            }
        }
        // Strangers start afresh.
        for (int i = m_.word(Machine::kShared, mission::kFirstStranger); i >= 0 && i < 32; ++i) {
            setRec8(i, rec::kLetter, 0);
            setRec8(i, rec::kTeam, 0);
            setRec8(i, rec::kFlags, static_cast<std::uint8_t>(rec8(i, rec::kFlags) & ~flag::kShown));
        }
    }
    const auto code = file(name + ".RDF");
    if (!code) {
        throw FormatError("the room's code is missing: " + name);
    }
    m_.loadRoomCode(Machine::kRoom, *code, true);
    pushHandlers(0, m_.word(Machine::kRoom, room::kLists), 0);
    loadWalkMap(name);

    for (auto& b : bodies_) {
        b = Body{};
    }
    for (int i = 0; i < 32; ++i) {
        const auto r = record(i);
        std::uint8_t f = static_cast<std::uint8_t>(m_.u8(r) & ~flag::kInUse);
        if ((rec8(i, rec::kTeam) & team::kShown) != 0) {
            f |= flag::kShown;
        }
        m_.set8(r, f);
    }
    queue_.clear();
    roomTicks_ = 0;
    area_ = -1;
    exitArmed_ = -1;
    action_ = 1;
    subject_ = -1;
    if (first) {
        post(ev::kMissionBegins);
    }
    // Timers left running in the last room stop.
    for (int i = 0; i < 8; ++i) {
        const auto at = static_cast<std::uint16_t>(spot::kTimers + 2 * i);
        if (m_.s16(at) > 0) {
            m_.set16(at, 0);
        }
    }
}

void Game::enterRoom(int spawn) {
    post(ev::kTick);
    if (spawn == -2) {
        post(ev::kRestored);
        return;
    }
    const auto seg = Machine::kRoom;
    std::uint16_t at = m_.word(seg, room::kWayIn);
    const std::uint16_t end = m_.word(seg, static_cast<std::uint16_t>(room::kWayIn + 2));
    while (at < end) {
        const int who = s16(m_.word(seg, at));
        int fx = s16(m_.word(seg, static_cast<std::uint16_t>(at + 2)));
        int fy = s16(m_.word(seg, static_cast<std::uint16_t>(at + 4)));
        int tx = fx;
        int ty = fy;
        at = static_cast<std::uint16_t>(at + 6);
        for (int guard = 0; guard < 32 && at < end; ++guard) {
            const int s = s16(m_.word(seg, at));
            if (s == -1) {
                at = static_cast<std::uint16_t>(at + 2);
                break;
            }
            if (s == spawn) {
                fx = s16(m_.word(seg, static_cast<std::uint16_t>(at + 2)));
                fy = s16(m_.word(seg, static_cast<std::uint16_t>(at + 4)));
                tx = s16(m_.word(seg, static_cast<std::uint16_t>(at + 6)));
                ty = s16(m_.word(seg, static_cast<std::uint16_t>(at + 8)));
            }
            at = static_cast<std::uint16_t>(at + 10);
        }
        if (p_.devTrace && who >= 0 && who < 32) {
            std::fprintf(stderr, "way in: %d team %02x letter %02x from %d,%d to %d,%d\n", who, rec8(who, rec::kTeam),
                         rec8(who, rec::kLetter), fx, fy, tx, ty);
        }
        if (who < 0 || who >= 32 || (rec8(who, rec::kTeam) & team::kOnTeam) == 0) {
            continue;
        }
        if (spawn == -1) {
            startAnim(who, "?tele", fx, fy, -1);
            if (who == kKirk) {
                setRec8(who, rec::kCode, kArrived);
            }
        } else {
            walk(who, fx, fy, tx, ty, who == kKirk ? kArrived : 0);
        }
        if (who == kKirk) {
            busy_ = true;
            m_.set8(spot::kBusy, 1);
        }
    }
    if (spawn == -1) {
        playEffect("TRANSMAT");
    }
}

void Game::drawAway() {
    std::vector<const away::Sprite*> sprites;
    for (int i = 0; i < 32; ++i) {
        const Body& b = bodies_[i];
        if (b.used && (rec8(i, rec::kFlags) & flag::kInUse) != 0 && b.sprite.bitmap) {
            sprites.push_back(&b.sprite);
        }
    }
    away::sortForDrawing(sprites);
    scene_.draw(p_.screen, sprites);
    p_.palette = roomPalette_;
}

void Game::playEffect(const std::string& name) {
    const std::string tag = upperName(name);
    std::optional<PlainSound> sound;
    if (voices_ && voices_->has(tag)) {
        try {
            sound = voices_->sound(tag);
        } catch (const FormatError&) {
        }
    }
    if (!sound) {
        sound = assets_.sound(tag);
    }
    if (sound) {
        p_.queueSound(SoundChannel::Effect, sound->rate, std::move(sound->samples), true);
    }
}

// --- What the player does ----------------------------------------------------

void Game::setCursor(const std::string& name) {
    if (name == cursorName_) {
        return;
    }
    cursorName_ = name;
    cursor_ = picture(name);
    if (cursor_) {
        p_.pointer = cursor_->pixels;
        p_.pointerHotX = cursor_->handleX;
        p_.pointerHotY = cursor_->handleY;
    }
}

// What is under the pointer: a person (its number), a hotspot (0x20 and
// up), or -1.
int Game::thingAt(int x, int y) {
    int best = -1;
    int bestDepth = -1;
    for (int i = 0; i < 32; ++i) {
        const Body& b = bodies_[i];
        if (!b.used || (rec8(i, rec::kFlags) & flag::kInUse) == 0 || !b.sprite.hit(x, y)) {
            continue;
        }
        if (b.sprite.depth >= bestDepth) {
            best = i;
            bestDepth = b.sprite.depth;
        }
    }
    if (best >= 0) {
        return best;
    }
    for (const auto& p : polygons(room::kHotspots, room::kHotspots + 2)) {
        if (p.contains(x, y)) {
            return p.id;
        }
    }
    return -1;
}

int Game::actionMenu() {
    const auto pic = picture("ACTION");
    if (!pic) {
        return -1;
    }
    const int w = pic->width();
    const int h = pic->height();
    const int left = std::clamp(p_.mouseX - w / 2, 0, 320 - w);
    const int top = std::clamp(p_.mouseY - h / 2, 0, 200 - h);
    p_.takeClicks();
    p_.keys.clear();
    p_.pointerTargets.clear();
    for (const auto& a : kActionAreas) {
        p_.pointerTargets.push_back(ClipRect{left + a.x0, top + a.y0, left + a.x1, top + a.y1});
    }
    setCursor("walk");
    int picked = -1;
    while (!p_.quit) {
        drawAway();
        pic->copyTo(p_.screen, left, top);
        present();
        const auto clicks = p_.takeClicks();
        bool cancel = (clicks & 2) != 0;
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEscape) {
                cancel = true;
            }
        }
        p_.keys.clear();
        if (cancel) {
            break;
        }
        if ((clicks & 1) != 0) {
            const int mx = p_.mouseX - left;
            const int my = p_.mouseY - top;
            for (const auto& a : kActionAreas) {
                if (mx >= a.x0 && mx <= a.x1 && my >= a.y0 && my <= a.y1) {
                    picked = a.action;
                    break;
                }
            }
            break;
        }
    }
    p_.pointerTargets.clear();
    return picked;
}

// The things the team carries, and the crew to use. Returns an item
// (0x40 and up), a person, or -1.
int Game::inventory(bool forUse) {
    struct Slot {
        int id;
        std::shared_ptr<const Bitmap> pic;
    };
    std::vector<Slot> slots;
    if (forUse) {
        for (int i = 0; i < kCrewSlots; ++i) {
            const auto letter = rec8(i, rec::kLetter);
            if (letter == 0 || (rec8(i, rec::kTeam) & team::kBeamed) == 0) {
                continue;
            }
            const std::string n = std::string("i") + static_cast<char>(letter) + "use";
            if (auto pic = picture(n)) {
                slots.push_back(Slot{i, pic});
            }
        }
    }
    for (int item = 0x40; item < 0x80; ++item) {
        const auto at = static_cast<std::uint16_t>(spot::kItems + 10 * item);
        if (m_.u8(at) == 0) {
            continue;
        }
        const std::string n = m_.text(static_cast<std::uint16_t>(at + 1), 9);
        if (n.empty()) {
            continue;
        }
        if (auto pic = picture(n)) {
            slots.push_back(Slot{item, pic});
        }
    }
    if (slots.empty()) {
        return -1;
    }
    constexpr int kCell = 32;
    constexpr int kColumns = 5;
    const int rows = (static_cast<int>(slots.size()) + kColumns - 1) / kColumns;
    const int w = kColumns * kCell + 8;
    const int h = rows * kCell + 8;
    const int left = (320 - w) / 2;
    const int top = std::max(2, (200 - h) / 2);
    p_.takeClicks();
    p_.keys.clear();
    p_.pointerTargets.clear();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const int cx = left + 4 + static_cast<int>(i % kColumns) * kCell;
        const int cy = top + 4 + static_cast<int>(i / kColumns) * kCell;
        p_.pointerTargets.push_back(ClipRect{cx, cy, cx + kCell - 1, cy + kCell - 1});
    }
    int picked = -1;
    while (!p_.quit) {
        drawAway();
        for (int y = top; y < top + h; ++y) {
            for (int x = left; x < left + w; ++x) {
                p_.screen.set(x, y, shade_[p_.screen.at(x, y)]);
            }
        }
        int hover = -1;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const int cx = left + 4 + static_cast<int>(i % kColumns) * kCell;
            const int cy = top + 4 + static_cast<int>(i / kColumns) * kCell;
            const auto& pic = *slots[i].pic;
            pic.draw(p_.screen, cx + (kCell - pic.width()) / 2 + pic.handleX, cy + (kCell - pic.height()) / 2 + pic.handleY);
            if (p_.mouseX >= cx && p_.mouseX < cx + kCell && p_.mouseY >= cy && p_.mouseY < cy + kCell) {
                hover = static_cast<int>(i);
                for (int k = 0; k < kCell; ++k) {
                    p_.screen.set(cx + k, cy, 0xFF);
                    p_.screen.set(cx + k, cy + kCell - 1, 0xFF);
                    p_.screen.set(cx, cy + k, 0xFF);
                    p_.screen.set(cx + kCell - 1, cy + k, 0xFF);
                }
            }
        }
        present();
        const auto clicks = p_.takeClicks();
        bool cancel = (clicks & 2) != 0;
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEscape) {
                cancel = true;
            }
        }
        p_.keys.clear();
        if (cancel) {
            break;
        }
        if ((clicks & 1) != 0) {
            if (hover >= 0) {
                picked = slots[static_cast<std::size_t>(hover)].id;
            }
            break;
        }
    }
    p_.pointerTargets.clear();
    return picked;
}

void Game::awayInput() {
    const int over = busy_ ? -1 : thingAt(p_.mouseX, p_.mouseY);
    setCursor(busy_ ? "wait" : cursorFor(action_, over >= 0 && action_ != 1));
    const auto clicks = p_.takeClicks();
    bool menu = (clicks & 2) != 0;
    bool inv = false;
    for (const auto& k : p_.keys) {
        if (k.scancode == scancode::kSpace || k.text == 'a' || k.text == 'A') {
            menu = true;
        } else if (k.text == 'i' || k.text == 'I') {
            inv = true;
        } else if (k.text == 'w' || k.text == 'W') {
            action_ = 1;
            subject_ = -1;
        } else if (k.text == 'l' || k.text == 'L') {
            action_ = 4;
        } else if (k.text == 't' || k.text == 'T') {
            action_ = 5;
        } else if (k.text == 'g' || k.text == 'G') {
            action_ = 3;
        } else if (k.text == 'u' || k.text == 'U') {
            action_ = 2;
            subject_ = -1;
        }
    }
    p_.keys.clear();
    if (busy_) {
        return;
    }
    if (menu) {
        const int a = actionMenu();
        if (a == kOptions) {
            inv = true;
            action_ = 4;
        } else if (a > 0) {
            action_ = a;
            subject_ = -1;
            if (a == 2) {
                inv = true;
            }
        }
    }
    if (inv) {
        const int got = inventory(action_ == 2);
        if (got >= 0) {
            if (action_ == 2) {
                subject_ = got;
            } else if (action_ == 4) {
                post(ev::kLook, static_cast<std::uint8_t>(got));
            }
        }
        return;
    }
    if ((clicks & 1) == 0) {
        return;
    }
    m_.set16(spot::kMouseX, static_cast<std::uint16_t>(p_.mouseX));
    m_.set16(spot::kMouseY, static_cast<std::uint16_t>(p_.mouseY));
    switch (action_) {
    case 1: {
        // The followers come along.
        for (int i = 1; i < 32; ++i) {
            if ((rec8(i, rec::kTeam) & team::kFollows) != 0) {
                m_.set16(static_cast<std::uint16_t>(record(i) + rec::kFollow), 1);
            }
        }
        if (over > 0 && over < 0x40) {
            post(ev::kWalk, static_cast<std::uint8_t>(over));
        } else {
            walk(kKirk, -1, -1, p_.mouseX, p_.mouseY, 0);
        }
        break;
    }
    case 2:
        if (over < 0) {
            break;
        }
        if (subject_ < 0) {
            subject_ = over;
        } else {
            post(ev::kUse, static_cast<std::uint8_t>(subject_), static_cast<std::uint8_t>(over));
            subject_ = -1;
            action_ = 1;
        }
        break;
    case 3:
    case 4:
    case 5:
        if (over >= 0) {
            post(static_cast<std::uint8_t>(action_), static_cast<std::uint8_t>(over));
            action_ = 1;
        }
        break;
    default:
        break;
    }
}

// --- The mission ------------------------------------------------------------

void Game::awayMission() {
    int number = m_.s16(ds::kRoom);
    int spawn = m_.s16(ds::kSpawn);
    m_.set16(ds::kSpawn, 0xFFFF);
    m_.set16(ds::kRoom, 0);
    away_ = true;
    busy_ = false;
    p_.pointerShown = true;
    bool first = true;
    struct Back {
        Game& g;
        ~Back() {
            g.away_ = false;
            g.cursorName_.clear();
        }
    } back{*this};
    while (!p_.quit) {
        try {
            loadRoom(number, first);
            first = false;
            enterRoom(spawn);
            while (!p_.quit) {
                if (ticksDue_ > 0) {
                    // Ticks missed while a script waited are not made up.
                    ticksDue_ = 0;
                    awayTick();
                }
                drawAway();
                present();
                awayInput();
            }
        } catch (const ChangeRoom& c) {
            number = c.room;
            spawn = c.spawn;
        } catch (const LeaveMission&) {
            return;
        }
    }
}

int Game::sayRoom(std::uint16_t at) {
    const auto seg = currentSeg_;
    const auto who = s16(m_.cpu.read16(seg, at));
    std::vector<std::string> texts;
    const auto first = m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 2));
    const auto second = m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 4));
    if (first != 0 && second == 0xFFFF) {
        texts.push_back(m_.scriptText(seg, first));
    } else {
        for (int i = 1; i < 16; ++i) {
            const auto off = m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 2 * i));
            if (off == 0) {
                break;
            }
            texts.push_back(m_.scriptText(seg, off));
        }
    }
    return say(who, texts, seg);
}

int Game::askText() {
    // A typed answer: the keys typed until Enter.
    std::string typed;
    p_.keys.clear();
    while (!p_.quit) {
        drawAway();
        const int w = 30 * SequelFont::kWidth + 12;
        const int x = (320 - w) / 2;
        const int y = 80;
        for (int yy = y; yy < y + 22; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                p_.screen.set(xx, yy, shade_[p_.screen.at(xx, yy)]);
            }
        }
        font_.drawText(p_.screen, x + 6, y + 6, typed + "_", 0);
        present();
        bool done = false;
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEnter) {
                done = true;
            } else if (k.scancode == scancode::kBackspace && !typed.empty()) {
                typed.pop_back();
            } else if (k.text >= 32 && k.text < 127 && typed.size() < 29) {
                typed.push_back(static_cast<char>(k.text));
            }
        }
        p_.keys.clear();
        if (done) {
            break;
        }
    }
    typedText_ = typed;
    return 0;
}

} // namespace helmsman::sequel::detail
