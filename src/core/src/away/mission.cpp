// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/away/mission.hpp"

#include "helmsman/away/menus.hpp"
#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>

namespace helmsman::away {

namespace {

// Where the mission's state lies in the block the rooms share with the
// game (offsets from its start).
namespace st {
constexpr int kTimers = 0x00;        // eight words, counted down each tick
constexpr int kStunned = 0x14;       // four words: how long each of the crew stays down
constexpr int kMouseX = 0x10;
constexpr int kMouseY = 0x12;
constexpr int kCrewEntering = 0x1D;
constexpr int kRedshirtDead = 0x1E;
constexpr int kAction = 0x1F;
constexpr int kActiveObject = 0x20;
constexpr int kNotHandled = 0x23;
constexpr int kFlags = 0x24;
constexpr int kCrewFacing = 0x25;    // four bytes
} // namespace st

constexpr char kCrewLetters[] = "ksmr";
constexpr char kFacingLetters[] = "nsew";

std::mt19937& randomEngine() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

int randomInt(int below) {
    if (below <= 1) {
        return 0;
    }
    return static_cast<int>(randomEngine()() % static_cast<unsigned>(below));
}

int roundFixed(std::int32_t v) { return static_cast<int>((static_cast<std::int64_t>(v) + 0x8000) >> 16); }

int isqrt(long long v) {
    if (v <= 0) {
        return 0;
    }
    auto r = static_cast<long long>(std::sqrt(static_cast<double>(v)));
    while (r * r > v) {
        --r;
    }
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return static_cast<int>(r);
}

} // namespace

Mission::Mission(Session& session, std::string name) : session_(session), name_(std::move(name)) {
    cpu_.host = [this](X86& cpu, std::uint16_t) { hostCall(cpu); };
    for (int i = 0; i < kStateSize; ++i) {
        var(i) = 0;
    }
    scene_.font = &session_.font;
}

// --- The room file -------------------------------------------------------

std::uint16_t Mission::rdf16(int offset) const {
    if (offset < 0 || static_cast<std::size_t>(offset) + 1 >= rdf_.size()) {
        return 0;
    }
    // Read where the room's code runs, as the code may have changed it.
    const std::uint8_t* room = cpu_.memory() + X86::linear(kRoomSeg, 0);
    return static_cast<std::uint16_t>(room[offset] | (room[offset + 1] << 8));
}

std::string Mission::rdfText(int offset, int max) const {
    std::string out;
    for (int i = 0; i < max; ++i) {
        const auto p = static_cast<std::size_t>(offset + i);
        const std::uint8_t* room = cpu_.memory() + X86::linear(kRoomSeg, 0);
        if (offset < 0 || p >= rdf_.size() || room[p] == 0) {
            break;
        }
        out.push_back(static_cast<char>(room[p]));
    }
    return out;
}

// --- Rooms -----------------------------------------------------------------

void Mission::loadRoom(int room) {
    setUpRoom(room);
}

void Mission::setUpRoom(int room) {
    room_ = room;
    const std::string base = upper(name_) + std::to_string(room);
    roomTicks_ = 0;
    var(st::kCrewEntering) = 0;
    session_.stopEffects();

    auto& archive = session_.archive;
    scene_.backdrop.fill(0);
    Bitmap::fromBytes(archive.need(base + ".BMP")).copyTo(scene_.backdrop, 0, 0);
    if (const auto pri = archive.load(base + ".PRI")) {
        scene_.setDepths(*pri);
    } else {
        std::fill(scene_.depths.begin(), scene_.depths.end(), 0);
    }
    session_.platform.palette = Palette::fromBytes(archive.need("PALETTE.PAL"));
    if (const auto lut = archive.load("PALETTE.LUT"); lut && lut->size() >= 256) {
        std::copy_n(lut->begin(), 256, scene_.darken.begin());
    } else {
        for (int i = 0; i < 256; ++i) {
            scene_.darken[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        }
    }

    rdf_ = archive.need(base + ".RDF");
    if (rdf_.size() < 0xCA || rdf_.size() > 0xFFF0) {
        throw FormatError("a room file has an odd size");
    }
    std::uint8_t* mem = cpu_.memory();
    std::memset(mem + X86::linear(kRoomSeg, 0), 0, 0x10000);
    std::memcpy(mem + X86::linear(kRoomSeg, 0), rdf_.data(), rdf_.size());
    // The rooms call the game through the far pointer at their start.
    cpu_.write16(kRoomSeg, 0, 0x0000);
    cpu_.write16(kRoomSeg, 2, X86::kHostSegment);
    cpu_.write16(kRoomSeg, 4, kState);

    var(st::kAction) = kWalk;
    loadWalkMap(base);
    clearActors();

    const int maxY = static_cast<std::int16_t>(rdf16(0x06));
    const int minY = static_cast<std::int16_t>(rdf16(0x08));
    const auto scaleSpan = static_cast<std::uint16_t>(rdf16(0x0C) - rdf16(0x0A));
    const int rows = maxY - minY + 1;
    scaleFactor_ = rows != 0 ? static_cast<int>(static_cast<double>(scaleSpan) * 256.0 / rows) : 0;

    bans_.clear();
    for (int p = rdf16(0x1E); p < rdf16(0x20) && p < static_cast<int>(rdf_.size());) {
        const std::string banName = rdfText(p, 16);
        p += static_cast<int>(banName.size()) + 1;
        if (!banName.empty()) {
            loadBan(banName);
        }
    }
    events_.clear();
    lastWarp_ = -1;
    lastTouched_ = -1;
    for (auto& q : queued_) {
        q.used = false;
    }
}

void Mission::loadWalkMap(const std::string& base) {
    walkMap_.clear();
    nodes_.clear();
    nextHop_.assign(32 * 32, 0);
    if (auto map = session_.archive.load(upper(base) + ".MAP")) {
        walkMap_ = std::move(*map);
    }
    if (const auto iw = session_.archive.load(upper(base) + ".IW")) {
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

bool Mission::blocked(int x, int y) const {
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

bool Mission::lineClear(int x0, int y0, int x1, int y1) const {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    int n = 0;
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (std::abs(dx) > std::abs(dy)) {
        n = std::abs(dx);
        if (n == 0) {
            return true;
        }
        sy = static_cast<std::int32_t>((static_cast<std::int64_t>(dy) << 16) / n);
        sx = dx > 0 ? 0x10000 : dx < 0 ? -0x10000 : 0;
    } else {
        n = std::abs(dy);
        if (n == 0) {
            return true;
        }
        sx = static_cast<std::int32_t>((static_cast<std::int64_t>(dx) << 16) / n);
        sy = dy > 0 ? 0x10000 : dy < 0 ? -0x10000 : 0;
    }
    std::int32_t fx = x0 << 16;
    std::int32_t fy = y0 << 16;
    if (blocked(roundFixed(fx), roundFixed(fy))) {
        return false;
    }
    while (n-- > 0) {
        fx += sx;
        fy += sy;
        if (blocked(roundFixed(fx), roundFixed(fy))) {
            return false;
        }
    }
    return true;
}

int Mission::nearestNode(int x, int y) const {
    std::vector<std::pair<int, int>> order; // distance, node
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        const long long dx = x - nodes_[static_cast<std::size_t>(i)].first;
        const long long dy = y - nodes_[static_cast<std::size_t>(i)].second;
        order.emplace_back(isqrt(dx * dx + dy * dy), i);
    }
    std::stable_sort(order.begin(), order.end(), [](auto a, auto b) { return a.first < b.first; });
    for (const auto& [d, i] : order) {
        const auto& n = nodes_[static_cast<std::size_t>(i)];
        if (lineClear(x, y, n.first, n.second)) {
            return i;
        }
    }
    return -1;
}

int Mission::scaleAt(int y) const {
    const int maxY = static_cast<std::int16_t>(rdf16(0x06));
    const int minY = static_cast<std::int16_t>(rdf16(0x08));
    if (maxY < y) {
        y = maxY;
    }
    if (minY > y) {
        y = minY;
    }
    return rdf16(0x0A) + static_cast<int>((static_cast<long long>(y - minY) * scaleFactor_) >> 8);
}

int Mission::depthOfRow(int y) const { return scene_.depthAt(0, std::clamp(y, 0, 199)); }

// --- Background animations -----------------------------------------------

void Mission::loadBan(const std::string& name) {
    if (bans_.size() >= 16) {
        return;
    }
    if (auto bytes = session_.archive.load(upper(name) + ".BAN")) {
        bans_.push_back(Ban{std::move(*bytes), 0});
    }
}

void Mission::updateBans(bool) {
    if ((frameTicks_ & 3) != 0) {
        return;
    }
    for (auto& ban : bans_) {
        const auto& d = ban.data;
        auto at16 = [&d](std::size_t p) -> int {
            return p + 1 < d.size() ? static_cast<std::int16_t>(d[p] | (d[p + 1] << 8)) : -1;
        };
        if (ban.pos + 4 > d.size() || at16(ban.pos) == -1) {
            ban.pos = 0;
        }
        if (d.size() < 4) {
            continue;
        }
        std::size_t p = ban.pos;
        const int start = at16(p);
        const int ops = at16(p + 2);
        if (ops == 0) {
            ban.pos = p + 4;
            continue;
        }
        p += 12;
        long pixel = static_cast<std::uint16_t>(start);
        std::uint8_t* screen = scene_.backdrop.data();
        const long screenSize = static_cast<long>(scene_.backdrop.size());
        auto put = [&](std::uint8_t c) {
            if (c != 0 && pixel >= 0 && pixel < screenSize) {
                screen[pixel] = c;
            }
            ++pixel;
        };
        for (int op = 0; op < ops && p < d.size(); ++op) {
            const auto b = static_cast<std::int8_t>(d[p++]);
            if (b >= 0) {
                for (int i = 0; i <= b && p < d.size(); ++i) {
                    put(d[p++]);
                }
            } else if (static_cast<std::uint8_t>(b) == 0x80) {
                pixel += at16(p) & 0xFFFF;
                p += 2;
            } else {
                const int n = -b + 1;
                const std::uint8_t c = p < d.size() ? d[p++] : 0;
                if (c == 0) {
                    pixel += n;
                } else {
                    for (int i = 0; i < n; ++i) {
                        put(c);
                    }
                }
            }
        }
        ban.pos = p;
    }
}

// --- Actors ----------------------------------------------------------------

std::string Mission::crewName(int crew, const std::string& base) const {
    std::string out(1, kCrewLetters[std::clamp(crew, 0, 3)]);
    return out + base;
}

void Mission::clearActors() {
    for (auto& a : actors_) {
        a = Actor{};
    }
    for (int i = 0; i < 4; ++i) {
        actors_[static_cast<std::size_t>(i)].standName = crewName(i, "stnd");
    }
}

void Mission::loadAnimInto(Actor& a, std::string name, int x, int y, int scale, bool) {
    if (name.size() > 5 && name.compare(1, 4, "stnd") == 0 && session_.assets.archive().has(upper(name) + "J.ANM")) {
        name += "j";
    }
    a.animName = name;
    a.state = Actor::State::Animating;
    a.frames = &session_.assets.anim(name);
    a.frame = 0;
    a.x = x;
    a.y = y;
    a.loops = 0;
    a.scale = scale;
    a.used = true;
    a.sprite.mode = SpriteMode::Normal;
    if (a.frames->empty()) {
        a.sprite.bitmap.reset();
        a.nextTick = frameTicks_ + 1;
        return;
    }
    const AnimFrame& f = a.frames->front();
    a.nextTick = frameTicks_ + static_cast<std::uint64_t>(f.ticks);
    a.pictureName = f.picture;
    try {
        a.sprite.bitmap = session_.assets.picture(f.picture, scale);
    } catch (const FormatError&) {
        a.sprite.bitmap.reset();
    }
    a.sprite.x = x + f.x;
    a.sprite.y = y + f.y;
    a.sprite.depth = depthOfRow(y + f.y) + f.depth;
}

int Mission::startAnim(int index, const std::string& name, int x, int y, int scale) {
    if (index == -1) {
        for (int i = 8; i < kActors; ++i) {
            if (!actors_[static_cast<std::size_t>(i)].used) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            return -1;
        }
    }
    if (index < 0 || index >= kActors) {
        return -1;
    }
    Actor& a = actors_[static_cast<std::size_t>(index)];
    loadAnimInto(a, name, x, y, scale, true);
    a.notifyFinish = false;
    a.finishEvent = 0;
    return index;
}

void Mission::removeActor(int index) {
    if (index < 0 || index >= kActors) {
        return;
    }
    Actor& a = actors_[static_cast<std::size_t>(index)];
    const std::string stand = a.standName;
    a = Actor{};
    a.standName = stand;
}

void Mission::stopActor(int index) {
    if (index < 0 || index >= kActors) {
        return;
    }
    if (actors_[static_cast<std::size_t>(index)].standName.empty()) {
        removeActor(index);
    } else {
        standActor(index);
    }
}

void Mission::standActor(int index) {
    Actor& a = actors_[static_cast<std::size_t>(index)];
    if (index >= 0 && index <= 3) {
        const std::uint8_t facing = var(st::kCrewFacing + index);
        if (facing != 0xFF) {
            a.dir = static_cast<char>(std::toupper(static_cast<unsigned char>(kFacingLetters[facing & 3])));
            var(st::kCrewFacing + index) = 0xFF;
        }
    }
    std::string name = a.standName;
    if (a.dir != 0) {
        name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(a.dir))));
    } else {
        name += "s";
    }
    const int x = a.x;
    const int y = a.y;
    startAnim(index, name, x, y, scaleAt(y));
    a.state = Actor::State::Standing;
}

void Mission::setWalkFrame(Actor& a, int x, int y) {
    a.scale = scaleAt(y);
    char name[32];
    std::snprintf(name, sizeof name, "%s%02d", a.walkName.c_str(), a.walkFrame & 7);
    a.pictureName = name;
    try {
        a.sprite.bitmap = session_.assets.picture(a.pictureName, a.scale);
    } catch (const FormatError&) {
        a.sprite.bitmap.reset();
    }
    a.sprite.mode = SpriteMode::Normal;
    a.sprite.x = x;
    a.sprite.y = y;
    a.sprite.depth = depthOfRow(y);
    a.nextTick = frameTicks_;
    a.x = x;
    a.y = y;
}

void Mission::startSegment(Actor& a, int sx, int sy, int dx, int dy) {
    a.fx = sx << 16;
    a.fy = sy << 16;
    const int ddx = dx - sx;
    const int ddy = dy - sy;
    if (std::abs(ddx) > std::abs(ddy)) {
        a.dir = ddx > 0 ? 'E' : 'W';
        a.walkName.push_back(static_cast<char>(std::tolower(a.dir)));
        a.steps = std::abs(ddx);
        if (a.steps == 0) {
            return;
        }
        a.vx = ddx > 0 ? 0x10000 : ddx < 0 ? -0x10000 : 0;
        a.vy = static_cast<std::int32_t>((static_cast<std::int64_t>(ddy) << 16) / a.steps);
    } else {
        a.dir = ddy > 0 ? 'S' : 'N';
        a.walkName.push_back(static_cast<char>(std::tolower(a.dir)));
        a.steps = std::abs(ddy);
        if (a.steps == 0) {
            return;
        }
        a.vx = static_cast<std::int32_t>((static_cast<std::int64_t>(ddx) << 16) / a.steps);
        a.vy = ddy > 0 ? 0x10000 : ddy < 0 ? -0x10000 : 0;
    }
}

bool Mission::walkActor(int index, const std::string& walkName, int sx, int sy, int dx, int dy) {
    if (index < 0 || index >= kActors) {
        return false;
    }
    Actor& a = actors_[static_cast<std::size_t>(index)];
    a.notifyFinish = false;
    if (blocked(dx, dy)) {
        return false;
    }
    a.used = true;
    a.state = Actor::State::Walking;
    a.nextTick = frameTicks_ + 1;
    a.walkName = walkName;
    a.destX = dx;
    a.destY = dy;
    a.walkFrame = 0;
    a.pathNode = -1;
    a.destNode = -1;
    a.frames = nullptr;
    if (lineClear(sx, sy, dx, dy)) {
        startSegment(a, sx, sy, dx, dy);
        setWalkFrame(a, roundFixed(a.fx), roundFixed(a.fy));
        return true;
    }
    a.pathNode = nearestNode(sx, sy);
    a.destNode = nearestNode(dx, dy);
    if (a.pathNode == -1 || a.destNode == -1) {
        a.walkName += "s";
        a.dir = 'S';
        setWalkFrame(a, sx, sy);
        standActor(index);
        return false;
    }
    const auto& n = nodes_[static_cast<std::size_t>(a.pathNode)];
    startSegment(a, sx, sy, n.first, n.second);
    setWalkFrame(a, roundFixed(a.fx), roundFixed(a.fy));
    return true;
}

void Mission::updateAnim(int index) {
    Actor& a = actors_[static_cast<std::size_t>(index)];
    if (frameTicks_ < a.nextTick) {
        return;
    }
    if (a.frames == nullptr || a.frames->empty()) {
        if (a.notifyFinish) {
            a.notifyFinish = false;
            post(kFinishedAnimation, static_cast<std::uint8_t>(a.finishEvent));
        }
        stopActor(index);
        return;
    }
    const int count = static_cast<int>(a.frames->size());
    const int next = (*a.frames)[static_cast<std::size_t>(std::min(a.frame, count - 1))].next[randomInt(4)];
    if (next != a.frame && next == count - 1) {
        ++a.loops;
        if (a.notifyFinish) {
            post(kFinishedAnimation, static_cast<std::uint8_t>(a.finishEvent));
        }
    }
    a.frame = next;
    if (a.frame >= count) {
        stopActor(index);
        return;
    }
    const AnimFrame& f = (*a.frames)[static_cast<std::size_t>(a.frame)];
    a.pictureName = f.picture;
    try {
        a.sprite.bitmap = session_.assets.picture(f.picture, a.scale);
    } catch (const FormatError&) {
        a.sprite.bitmap.reset();
    }
    a.sprite.x = a.x + f.x;
    a.sprite.y = a.y + f.y;
    a.sprite.depth = depthOfRow(a.y + f.y) + f.depth;
    a.nextTick = frameTicks_ + static_cast<std::uint64_t>(f.ticks);
}

void Mission::updateWalk(int index) {
    Actor& a = actors_[static_cast<std::size_t>(index)];
    if (frameTicks_ < a.nextTick) {
        return;
    }
    if (index == 0) {
        checkTouches();
    }
    if (a.steps > 0) {
        const int perTick = scaleAt(roundFixed(a.fy)) < 0xA0 ? 1 : 2;
        for (int k = 0; k < perTick && a.steps > 0; ++k) {
            --a.steps;
            const std::int32_t nx = a.fx + a.vx;
            const std::int32_t ny = a.fy + a.vy;
            if ((a.steps & 3) == 0) {
                setWalkFrame(a, roundFixed(nx), roundFixed(ny));
                ++a.walkFrame;
            }
            a.fx = nx;
            a.fy = ny;
            a.nextTick = frameTicks_;
        }
        return;
    }
    if (a.pathNode == -1) {
        if (a.notifyFinish) {
            a.notifyFinish = false;
            post(kFinishedWalking, static_cast<std::uint8_t>(a.finishEvent & 0xFF));
        }
        setWalkFrame(a, roundFixed(a.fx), roundFixed(a.fy));
        standActor(index);
        return;
    }
    if (!a.walkName.empty()) {
        a.walkName.pop_back();
    }
    if (a.pathNode == a.destNode) {
        a.pathNode = -1;
        a.destNode = -1;
        startSegment(a, a.x, a.y, a.destX, a.destY);
        return;
    }
    a.pathNode = nextHop_[static_cast<std::size_t>((a.pathNode & 31) * 32 + (a.destNode & 31))];
    if (a.pathNode < 0 || a.pathNode >= static_cast<int>(nodes_.size())) {
        a.pathNode = -1;
        a.destNode = -1;
        startSegment(a, a.x, a.y, a.destX, a.destY);
        return;
    }
    const auto& n = nodes_[static_cast<std::size_t>(a.pathNode)];
    startSegment(a, a.x, a.y, n.first, n.second);
}

void Mission::updateActors() {
    for (int i = 0; i < kActors; ++i) {
        Actor& a = actors_[static_cast<std::size_t>(i)];
        if (!a.used) {
            continue;
        }
        if (a.state == Actor::State::Walking) {
            updateWalk(i);
        } else {
            updateAnim(i);
        }
    }
}

void Mission::spawnCrew(int how) {
    for (int i = 0; i < 4; ++i) {
        var(st::kCrewFacing + i) = 0xFF;
    }
    const int crew = var(st::kRedshirtDead) != 0 ? 3 : 4;
    if (how >= 0 && how <= 3) {
        for (int i = 0; i < crew; ++i) {
            const int at = 0x2A + how * 32 + i * 8;
            walkActor(i, crewName(i, "walk"), static_cast<std::int16_t>(rdf16(at)), static_cast<std::int16_t>(rdf16(at + 2)),
                      static_cast<std::int16_t>(rdf16(at + 4)), static_cast<std::int16_t>(rdf16(at + 6)));
        }
        actors_[0].notifyFinish = true;
        actors_[0].finishEvent = 0xFF;
        var(st::kCrewEntering) = 1;
        warpsEnabled_ = false;
    } else if (how == 4) {
        for (int i = 0; i < crew; ++i) {
            const int x = static_cast<std::int16_t>(rdf16(0xAA + i * 4));
            const int y = static_cast<std::int16_t>(rdf16(0xAC + i * 4));
            startAnim(i, crewName(i, "tele"), x, y, scaleAt(y));
        }
        actors_[0].notifyFinish = true;
        actors_[0].finishEvent = 0xFF;
        var(st::kCrewEntering) = 1;
        warpsEnabled_ = false;
        session_.playEffect("TRANSMAT");
    } else if (how == 5) {
        for (int i = 0; i < crew; ++i) {
            const int x = static_cast<std::int16_t>(rdf16(0xBA + i * 4));
            const int y = static_cast<std::int16_t>(rdf16(0xBC + i * 4));
            startAnim(i, crewName(i, "stnds"), x, y, scaleAt(y));
        }
        warpsEnabled_ = true;
    }
}

// --- Events ----------------------------------------------------------------

void Mission::post(std::uint8_t action, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3) {
    if (events_.size() >= 64) {
        events_.pop_front();
    }
    events_.push_back(Event{action, b1, b2, b3});
}

void Mission::runCode(std::uint16_t offset) {
    if (inRoomCode_) {
        return;
    }
    inRoomCode_ = true;
    // Cleared however the code is left, also when the game is stopped in
    // the middle of it.
    struct Leave {
        bool& flag;
        ~Leave() { flag = false; }
    } leave{inRoomCode_};
    cpu_.s[X86::DS] = kDataSeg;
    cpu_.s[X86::ES] = kDataSeg;
    cpu_.s[X86::SS] = kDataSeg;
    cpu_.r[X86::SP] = 0xFFF0;
    cpu_.r[X86::BP] = 0xFFF0;
    cpu_.r[X86::SI] = kState;
    cpu_.r[X86::BX] = kState;
    cpu_.stopRequested = false;
    setWord(st::kMouseX, static_cast<std::uint16_t>(session_.platform.mouseX));
    setWord(st::kMouseY, static_cast<std::uint16_t>(session_.platform.mouseY));
    try {
        cpu_.callFar(kRoomSeg, offset);
    } catch (const CpuError&) {
        // A room's code the interpreter cannot run is skipped.
    }
}

bool Mission::runExact(const Event& e) {
    const std::uint32_t want = e.action | (e.b1 << 8) | (e.b2 << 16) | (static_cast<std::uint32_t>(e.b3) << 24);
    int guard = 0;
    for (int p = rdf16(0x0E); p != 0 && p < rdf16(0x10) && guard < 4096; ++guard) {
        const std::uint32_t have = rdf16(p) | (static_cast<std::uint32_t>(rdf16(p + 2)) << 16);
        const int next = rdf16(p + 4);
        if (have == want) {
            var(st::kNotHandled) = 0;
            runCode(static_cast<std::uint16_t>(p + 6));
            if (var(st::kNotHandled) == 0) {
                return true;
            }
        }
        if (next <= p) {
            break;
        }
        p = next;
    }
    return false;
}

std::vector<Event> Mission::roomEvents() const {
    std::vector<Event> out;
    int guard = 0;
    for (int p = rdf16(0x0E); p != 0 && p < rdf16(0x10) && guard < 4096; ++guard) {
        out.push_back(Event{static_cast<std::uint8_t>(rdf16(p) & 0xFF), static_cast<std::uint8_t>(rdf16(p) >> 8),
                            static_cast<std::uint8_t>(rdf16(p + 2) & 0xFF), static_cast<std::uint8_t>(rdf16(p + 2) >> 8)});
        const int next = rdf16(p + 4);
        if (next <= p) {
            break;
        }
        p = next;
    }
    return out;
}

std::vector<Event> Mission::exitEvents() const {
    std::vector<Event> out;
    if (cpu_.memory()[X86::linear(kDataSeg, static_cast<std::uint16_t>(kState + st::kFlags))] == 0 && warpsEnabled_) {
        int guard = 0;
        for (int p = rdf16(0x16); p != 0 && p < rdf16(0x18) && guard < 256; ++guard) {
            out.push_back(Event{kTouchedWarp, static_cast<std::uint8_t>(rdf16(p) & 0xFF), 0, 0});
            p += 4 + rdf16(p + 2) * 4;
        }
    }
    return out;
}

bool Mission::hasAction(const Event& e) const {
    const std::uint32_t want = e.action | (e.b1 << 8) | (e.b2 << 16) | (static_cast<std::uint32_t>(e.b3) << 24);
    int guard = 0;
    for (int p = rdf16(0x0E); p != 0 && p < rdf16(0x10) && guard < 4096; ++guard) {
        const std::uint32_t have = rdf16(p) | (static_cast<std::uint32_t>(rdf16(p + 2)) << 16);
        if (have == want) {
            return true;
        }
        const int next = rdf16(p + 4);
        if (next <= p) {
            break;
        }
        p = next;
    }
    return false;
}

bool Mission::runMatching(const Event& e) {
    const std::uint32_t want = e.action | (e.b1 << 8) | (e.b2 << 16) | (static_cast<std::uint32_t>(e.b3) << 24);
    int guard = 0;
    for (int p = rdf16(0x0E); p != 0 && p < rdf16(0x10) && guard < 4096; ++guard) {
        const std::uint32_t have = rdf16(p) | (static_cast<std::uint32_t>(rdf16(p + 2)) << 16);
        const int next = rdf16(p + 4);
        std::uint32_t mask = 0;
        for (int i = 0; i < 4; ++i) {
            if (((have >> (8 * i)) & 0xFF) != 0xFF) {
                mask |= 0xFFu << (8 * i);
            }
        }
        if ((have & mask) == (want & mask)) {
            var(st::kNotHandled) = 0;
            runCode(static_cast<std::uint16_t>(p + 6));
            if (var(st::kNotHandled) == 0) {
                return true;
            }
        }
        if (next <= p) {
            break;
        }
        p = next;
    }
    return false;
}

void Mission::processEvents() {
    int guard = 0;
    while (!events_.empty() && guard++ < 256 && !pendingChange_ && !result_) {
        const Event e = events_.front();
        events_.pop_front();
        handleEvent(e);
    }
}

void Mission::handleEvent(const Event& e) {
    if (session_.trace && e.action != kTick) {
        std::fprintf(stderr, "[%llu] event %d %02x %02x %02x\n", static_cast<unsigned long long>(session_.platform.millis / 16), e.action, e.b1, e.b2, e.b3);
    }
    if ((e.action == kFinishedWalking || e.action == kFinishedAnimation) && e.b1 == 0xFF) {
        if (var(st::kCrewEntering) == 1) {
            var(st::kCrewEntering) = 0;
        }
        warpsEnabled_ = true;
        return;
    }
    if (e.action == kFinishedWalking && e.b1 >= 0xE0) {
        Queued& q = queued_[static_cast<std::size_t>((e.b1 - 0xE0) & 31)];
        if (q.used) {
            q.used = false;
            post(q.event.action, q.event.b1, q.event.b2, q.event.b3);
        }
        return;
    }
    if (runExact(e)) {
        return;
    }
    defaultResponse(e);
}

void Mission::defaultResponse(const Event& e) {
    switch (e.action) {
    case kWalk: {
        if (runMatching(e)) {
            return;
        }
        const Actor& kirk = actors_[0];
        walkActor(0, crewName(0, "walk"), kirk.x, kirk.y, session_.platform.mouseX, session_.platform.mouseY);
        return;
    }
    case kUse:
        if (e.b1 == e.b2) {
            return;
        }
        switch (e.b1) {
        case 0:
            if (!runExact(Event{kWalk, e.b2}) && !runExact(Event{kGet, e.b2})) {
                showDefault("Capt. Kirk", 0x50, 0xB0);
            }
            return;
        case 1:
            if (!runExact(Event{kUse, 0x44, e.b2})) {
                showDefault("Spock", 0x51, 0xC0);
            }
            return;
        case 2:
            if (!runExact(Event{kUse, 0x7F, e.b2}) && !runExact(Event{kUse, 0x45, e.b2})) {
                showDefault("McCoy", 0x52, 0xC0);
            }
            return;
        case 3:
            showDefault("", 0x53, 0xB0);
            return;
        case 0x40:
        case 0x41: {
            static constexpr int kSpock[8] = {0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
            static constexpr int kMcCoy[8] = {0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E};
            static constexpr int kRedshirt[8] = {0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E};
            static constexpr int kOthers[7] = {0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75};
            if (e.b2 == 1) {
                showDefault("Spock", kSpock[randomInt(8)], 0xC0);
            } else if (e.b2 == 2) {
                showDefault("McCoy", kMcCoy[randomInt(8)], 0xC0);
            } else if (e.b2 == 3) {
                std::string line = session_.textLine("ground", kRedshirt[randomInt(8)]);
                // The security officer of each mission has his own voice.
                for (int i = 0; i < 3 && i < static_cast<int>(name_.size()) && line.size() > 8; ++i) {
                    line[static_cast<std::size_t>(6 + i)] = static_cast<char>(std::toupper(static_cast<unsigned char>(name_[static_cast<std::size_t>(i)])));
                }
                showText({"Security Officer", line}, 0x14, 0x14, 0xA1, false);
            } else if (!runMatching(e)) {
                const int pick = randomInt(7);
                showDefault((pick & 1) != 0 ? "McCoy" : "Spock", kOthers[pick], 0xC0);
            }
            return;
        }
        case 0x44:
            showDefault("Spock", 0x54, 0xC0);
            return;
        case 0x45:
            showDefault("McCoy", 0x55, 0xC0);
            return;
        case 0x7F:
            showDefault("McCoy", 0x56, 0xC0);
            return;
        case 0x47:
            if (!runExact(Event{kUse, 0x47, 0xFF})) {
                session_.playEffect("COMMUNIC");
                showDefault("Uhura", 0x76, 0xA1);
            }
            return;
        default:
            if (!runMatching(e)) {
                showDefault("", 0x77, 0xB0);
            }
            return;
        }
    case kGet:
        if (!runMatching(e)) {
            showDefault("", 0x78, 0xB0);
        }
        return;
    case kLook:
        if (e.b1 >= kFirstItem && e.b1 < kFirstItem + kItemCount) {
            showDefault("", itemInfo(e.b1).description, 0xB0);
        } else if (e.b1 <= 3) {
            showDefault("", 0x49 + e.b1, 0xB0);
        } else {
            showDefault("", 0x4D, 0xB0);
        }
        return;
    case kTalk:
        showDefault("", e.b1 <= 3 ? 0x4E : 0x4F, 0xB0);
        return;
    case kTouchedWarp: {
        if (runMatching(e)) {
            return;
        }
        const int room = rdf16(0x22 + e.b1 * 2);
        // 0xFFFF: a way out that leads nowhere unless the room's code says.
        if (room >= 0x8000) {
            return;
        }
        pendingChange_ = RoomChange{room, e.b1 ^ 1};
        return;
    }
    default:
        runMatching(e);
        return;
    }
}

// --- Room code calling back ------------------------------------------------

void Mission::hostCall(X86& cpu) {
    const int fn = cpu.arg(0);
    std::array<std::uint16_t, 6> a{};
    for (int i = 0; i < 6; ++i) {
        a[static_cast<std::size_t>(i)] = cpu.arg(i + 1);
    }
    const std::uint32_t result = roomFunction(fn, a);
    cpu.r[X86::AX] = static_cast<std::uint16_t>(result);
    cpu.r[X86::DX] = static_cast<std::uint16_t>(result >> 16);
    if (pendingChange_ || result_) {
        // A new room or the mission's end: the room's code does not go on.
        cpu.stopRequested = true;
    }
}

std::uint32_t Mission::roomFunction(int fn, const std::array<std::uint16_t, 6>& a) {
    if (session_.trace) {
        std::fprintf(stderr, "[%llu]  call %d (%x %x %x %x %x %x)\n", static_cast<unsigned long long>(session_.platform.millis / 16), fn, a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    auto s = [&a](int i) { return static_cast<int>(static_cast<std::int16_t>(a[static_cast<std::size_t>(i - 1)])); };
    switch (fn) {
    case 0:
    case 2: { // an actor plays an animation
        int index = s(1);
        int x = s(3);
        int y = s(4);
        if ((x == -1 || y == -1) && index >= 0 && index < kActors) {
            x = actors_[static_cast<std::size_t>(index)].sprite.x;
            y = actors_[static_cast<std::size_t>(index)].sprite.y;
        }
        const std::string anim = lower(rdfText(a[1], 10));
        if (index >= 0 && index <= 7) {
            index = startAnim(index, anim, x, y, scaleAt(y));
        } else {
            index = startAnim(index, anim, x, y, 256);
        }
        if (index >= 0 && a[4] != 0) {
            Actor& actor = actors_[static_cast<std::size_t>(index)];
            actor.notifyFinish = true;
            actor.finishEvent = a[4];
        }
        return 0;
    }
    case 1: // an actor stops
        if (var(st::kRedshirtDead) != 0 && s(1) == 3) {
            removeActor(3);
        } else {
            stopActor(s(1));
        }
        return 0;
    case 3:
        return static_cast<std::uint32_t>(showTextList(a[0]));
    case 4: { // is an item carried?
        const int i = s(1) - kFirstItem;
        return i >= 0 && i < kItemCount && session_.carried[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    case 5:
        showPicture("machineb");
        return 0;
    case 6: // to another room
        if (var(st::kFlags) != 0) {
            return 0;
        }
        pendingChange_ = RoomChange{s(1), s(2) % 6};
        return 0;
    case 7: { // the crew gets an item
        const int i = s(1) - kFirstItem;
        if (i >= 0 && i < kItemCount) {
            session_.carried[static_cast<std::size_t>(i)] = true;
        }
        if (var(st::kAction) == kUse && var(st::kActiveObject) == s(1)) {
            var(st::kAction) = kWalk;
            setCursor(kWalk, false);
        }
        return 0;
    }
    case 8: { // a crewman walks
        const int crew = s(1);
        if (crew < 0 || crew > 3) {
            return 0;
        }
        Actor& actor = actors_[static_cast<std::size_t>(crew)];
        const bool walking = walkActor(crew, crewName(crew, "walk"), actor.x, actor.y, s(2), s(3));
        if (walking && a[3] != 0) {
            actor.notifyFinish = true;
            actor.finishEvent = a[3];
        }
        return 0;
    }
    case 9: // another walking map
        loadWalkMap(rdfText(a[0], 16));
        return 0;
    case 0xA:
        showPicture(rdfText(a[0], 10));
        return 0;
    case 0xB: { // the crew loses an item
        const int i = s(1) - kFirstItem;
        if (i >= 0 && i < kItemCount) {
            session_.carried[static_cast<std::size_t>(i)] = false;
        }
        return 0;
    }
    case 0xC:
        showPicture("stlcase");
        return 0;
    case 0xD: { // where an actor stands
        const int i = s(1);
        if (i < 0 || i >= kActors) {
            return 0;
        }
        const Actor& actor = actors_[static_cast<std::size_t>(i)];
        return static_cast<std::uint16_t>(actor.sprite.x) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(actor.sprite.y)) << 16);
    }
    case 0xE: { // a random number
        const int lo = s(1);
        const int hi = s(2);
        if (hi < lo) {
            return static_cast<std::uint16_t>(lo);
        }
        return static_cast<std::uint16_t>(lo + randomInt(hi - lo + 1));
    }
    case 0xF: { // a sound effect by number
        static const char* const kSounds[] = {nullptr, nullptr, nullptr, nullptr, "TRICORDE", "STDOOR1", "PHASSHOT",
                                              nullptr, "TRANSDEM", "TRANSMAT", "TRANSENE"};
        const int n = s(1);
        if (n >= 0 && n < static_cast<int>(std::size(kSounds)) && kSounds[n] != nullptr) {
            session_.playEffect(kSounds[n]);
        } else if (n == 0x22) {
            session_.playEffect("HAILING");
        } else if (n == 0x24) {
            session_.playEffect("PHASSHOT");
        } else if (n == 0x25) {
            session_.playEffect("PHOTSHOT");
        } else if (n == 0x26) {
            session_.playEffect("HITSHIEL");
        } else if (n == 0x28) {
            session_.playEffect("REDALERT");
        } else if (n == 0x29) {
            session_.playEffect("WARP");
        }
        return 0;
    }
    case 0x10: // a song of the mission music, or quiet (below 0)
        if (s(1) < 0) {
            session_.stopMusic();
        } else {
            session_.loadMusic("ground");
            session_.playMusic(s(1));
        }
        return 0;
    case 0x11:
        endMission(s(1), s(2), s(3));
        return 0;
    case 0x12: { // the game is lost
        if (autoplay) {
            MissionResult r;
            r.quit = true;
            lost_ = true;
            result_ = r;
            return 0;
        }
        Ui u = ui();
        int load = -1;
        MissionResult r;
        switch (gameOverMenu(u, load)) {
        case GameOverChoice::Load: r.loadSave = load; break;
        case GameOverChoice::Restart: r.restart = true; break;
        case GameOverChoice::Quit: r.quit = true; break;
        }
        result_ = r;
        return 0;
    }
    case 0x13: { // the player types an answer; which of the room's answers it is
        // The answers follow each other in at most 30 bytes, each ending in a
        // NUL, and an empty one ends the list.
        std::vector<std::string> answers;
        int at = a[0];
        while (at - a[0] < 30) {
            const std::string one = rdfText(at, 30 - (at - a[0]));
            if (one.empty()) {
                break;
            }
            answers.push_back(lower(one));
            at += static_cast<int>(one.size()) + 1;
        }
        if (autoplay) {
            return answers.empty() ? 0 : 1 + autoAnswer % static_cast<int>(answers.size());
        }
        Ui u = ui();
        const auto typed = away::textInput(u, "Code:", 0xA0, 0x64, 20);
        if (!typed) {
            return 0;
        }
        const std::string said = lower(*typed);
        for (std::size_t i = 0; i < answers.size(); ++i) {
            if (answers[i] == said) {
                return static_cast<int>(i) + 1;
            }
        }
        return 0;
    }
    case 0x14: // the turbolift's map
        turbolift(s(2));
        return 0;
    case 0x15: // a sound effect by name, over and over
        session_.playEffect(rdfText(a[0], 10), true);
        return 0;
    case 0x17:
        session_.stopEffects();
        return 0;
    default:
        return 0;
    }
}

void Mission::endMission(int a, int b, int c) {
    MissionResult r;
    r.score = a;
    r.a = b;
    r.b = c;
    // The crew beams up.
    var(st::kCrewEntering) = 1;
    const int crew = var(st::kRedshirtDead) != 0 ? 3 : 4;
    for (int i = 0; i < crew; ++i) {
        Actor& actor = actors_[static_cast<std::size_t>(i)];
        startAnim(i, crewName(i, "teled"), actor.sprite.x, actor.sprite.y, scaleAt(actor.sprite.y));
        actor.standName.clear();
    }
    session_.playEffect("TRANSDEM");
    int guard = 0;
    while (actors_[0].used && guard++ < 400) {
        session_.waitTick();
        tickOnce();
        drawFrame();
    }
    var(st::kCrewEntering) = 0;
    result_ = r;
}

std::string Mission::describe() const {
    char head[120];
    std::snprintf(head, sizeof head, "action %d entering %d flags %02x events %d\n", cpu_.memory()[X86::linear(kDataSeg, kState + st::kAction)],
                  cpu_.memory()[X86::linear(kDataSeg, kState + st::kCrewEntering)], cpu_.memory()[X86::linear(kDataSeg, kState + 0x24)],
                  static_cast<int>(events_.size()));
    std::string out = head;
    for (int i = 0; i < kActors; ++i) {
        const Actor& a = actors_[static_cast<std::size_t>(i)];
        if (!a.used) {
            continue;
        }
        char line[200];
        std::snprintf(line, sizeof line, "%d: state %d anim %s pic %s at %d,%d sprite %d,%d depth %d frames %d bitmap %s\n", i,
                      static_cast<int>(a.state), a.animName.c_str(), a.pictureName.c_str(), a.x, a.y, a.sprite.x, a.sprite.y,
                      a.sprite.depth, a.frames ? static_cast<int>(a.frames->size()) : -1, a.sprite.bitmap ? "yes" : "no");
        out += line;
        int l = 0, t = 0, w = 0, h = 0;
        if (a.sprite.bounds(l, t, w, h)) {
            std::snprintf(line, sizeof line, "   bounds %d,%d %dx%d mode %d\n", l, t, w, h, static_cast<int>(a.sprite.mode));
            out += line;
        }
    }
    return out;
}

// --- The loop ----------------------------------------------------------------

void Mission::tickOnce() {
    updateActors();
    updateCursorHighlight();
    updateBans(false);
    // The room's timers.
    for (int i = 0; i < 8; ++i) {
        const std::uint16_t t = word(st::kTimers + i * 2);
        if (t != 0) {
            setWord(st::kTimers + i * 2, static_cast<std::uint16_t>(t - 1));
            if (t == 1) {
                post(kTimerExpired, static_cast<std::uint8_t>(i));
            }
        }
    }
    recoverStunned();
    ++frameTicks_;
    ++roomTicks_;
    post(kTick, static_cast<std::uint8_t>(roomTicks_ & 0xFF), static_cast<std::uint8_t>((roomTicks_ >> 8) & 0xFF));
}

// A crew member knocked down (their bit in the flags) lies there while
// their count runs down, then gets up and can go on.
void Mission::recoverStunned() {
    const int down = var(st::kFlags);
    if (down == 0) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        const int bit = 1 << i;
        if ((down & bit) == 0) {
            continue;
        }
        const auto left = static_cast<std::int16_t>(word(st::kStunned + i * 2) - 1);
        setWord(st::kStunned + i * 2, static_cast<std::uint16_t>(left));
        if (left > 0) {
            continue;
        }
        Actor& a = actors_[static_cast<std::size_t>(i)];
        // Facing the way the room asked for, or the way they lie.
        const int facing = var(st::kCrewFacing + i);
        char dir = static_cast<char>(std::tolower(static_cast<unsigned char>(a.dir)));
        if (facing != 0xFF) {
            dir = "nsew"[facing & 3];
        }
        int dx = 0;
        int dy = 0;
        if (dir == 'n') {
            dx = -0x18;
            dy = -8;
        } else if (dir == 'w') {
            dx = -0x23;
            dy = -0x0C;
        }
        const int scale = scaleAt(a.y);
        const int x = a.x + (scale * dx >> 8);
        const int y = a.y + (scale * dy >> 8);
        std::string name;
        name += kCrewLetters[i];
        name += "getu";
        name += dir;
        startAnim(i, name, x, y, scaleAt(y));
        var(st::kFlags) = static_cast<std::uint8_t>(var(st::kFlags) & ~bit);
    }
}

void Mission::drawFrame() {
    std::vector<const Sprite*> sprites;
    for (const auto& a : actors_) {
        if (a.used && a.sprite.bitmap) {
            sprites.push_back(&a.sprite);
        }
    }
    for (const Sprite* s : extraSprites_) {
        sprites.push_back(s);
    }
    scene_.draw(session_.platform.screen, sprites);
}

MissionResult Mission::run(int room, int spawn) {
    setUpRoom(room);
    spawnCrew(spawn);
    return loop();
}

MissionResult Mission::loop() {
    setCursor(kWalk, false);
    session_.platform.pointerShown = true;
    while (true) {
        processEvents();
        if (result_) {
            return *result_;
        }
        if (pendingChange_) {
            const RoomChange change = *pendingChange_;
            pendingChange_.reset();
            setUpRoom(change.room);
            spawnCrew(change.spawn);
            continue;
        }
        handleInput();
        if (result_) {
            return *result_;
        }
        if (session_.platform.quit) {
            MissionResult r;
            r.quit = true;
            return r;
        }
        session_.waitTick();
        tickOnce();
        drawFrame();
    }
}

// --- Saved games ----------------------------------------------------------------

namespace {
constexpr std::uint32_t kMissionSaveVersion = 1;
}

void Mission::save(SaveWriter& w) const {
    w.u32(kMissionSaveVersion);
    w.text(name_);
    w.s32(room_);
    w.u32(static_cast<std::uint32_t>(roomTicks_));
    w.u8(warpsEnabled_ ? 1 : 0);
    // The mission's state, and the room's code and data as they are now.
    const std::uint8_t* mem = cpu_.memory();
    w.bytes(std::span<const std::uint8_t>(mem + X86::linear(kDataSeg, kState), kStateSize));
    w.bytes(std::span<const std::uint8_t>(mem + X86::linear(kRoomSeg, 0), rdf_.size()));
    // Who is in the room, and what they are doing.
    int used = 0;
    for (const auto& a : actors_) {
        used += a.used ? 1 : 0;
    }
    w.u8(static_cast<std::uint8_t>(used));
    for (int i = 0; i < kActors; ++i) {
        const Actor& a = actors_[static_cast<std::size_t>(i)];
        if (!a.used) {
            continue;
        }
        w.u8(static_cast<std::uint8_t>(i));
        w.u8(static_cast<std::uint8_t>(a.state));
        w.text(a.animName);
        w.text(a.standName);
        w.s32(a.frame);
        w.s32(a.x);
        w.s32(a.y);
        w.s32(a.scale);
        w.u8(static_cast<std::uint8_t>(a.sprite.mode));
        w.u8(a.notifyFinish ? 1 : 0);
        w.s32(a.finishEvent);
        w.s32(a.destX);
        w.s32(a.destY);
        w.u8(static_cast<std::uint8_t>(a.dir));
    }
    // Events still to come.
    w.u8(static_cast<std::uint8_t>(std::min<std::size_t>(events_.size(), 64)));
    for (std::size_t i = 0; i < events_.size() && i < 64; ++i) {
        const Event& e = events_[i];
        w.u8(e.action);
        w.u8(e.b1);
        w.u8(e.b2);
        w.u8(e.b3);
    }
}

MissionResult Mission::resume(SaveReader& r) {
    restore(r);
    return loop();
}

void Mission::restore(SaveReader& r) {
    result_.reset();
    lost_ = false;
    pendingChange_.reset();
    if (r.u32() != kMissionSaveVersion) {
        throw FormatError("a saved game of another version");
    }
    name_ = r.text();
    const int room = r.s32();
    setUpRoom(room);
    roomTicks_ = r.u32();
    warpsEnabled_ = r.u8() != 0;
    const auto state = r.blob(kStateSize);
    const auto roomData = r.blob(0x10000);
    if (state.size() != static_cast<std::size_t>(kStateSize) || roomData.size() != rdf_.size()) {
        throw FormatError("the saved room does not match the game's room");
    }
    std::uint8_t* mem = cpu_.memory();
    std::memcpy(mem + X86::linear(kDataSeg, kState), state.data(), state.size());
    std::memcpy(mem + X86::linear(kRoomSeg, 0), roomData.data(), roomData.size());
    // The room's link to the game, in case the save came from elsewhere.
    cpu_.write16(kRoomSeg, 0, 0x0000);
    cpu_.write16(kRoomSeg, 2, X86::kHostSegment);
    cpu_.write16(kRoomSeg, 4, kState);

    clearActors();
    const int used = r.u8();
    for (int n = 0; n < used; ++n) {
        const int i = r.u8();
        const int st = r.u8();
        const std::string anim = r.text();
        const std::string stand = r.text();
        const int frame = r.s32();
        const int x = r.s32();
        const int y = r.s32();
        const int scale = r.s32();
        const int mode = r.u8();
        const bool notify = r.u8() != 0;
        const int finish = r.s32();
        const int destX = r.s32();
        const int destY = r.s32();
        const char dir = static_cast<char>(r.u8());
        if (i < 0 || i >= kActors) {
            throw FormatError("a saved actor out of range");
        }
        Actor& a = actors_[static_cast<std::size_t>(i)];
        if (st == static_cast<int>(Actor::State::Walking) && i <= 3) {
            // Walking on from where they were.
            a.standName = stand;
            walkActor(i, crewName(i, "walk"), x, y, destX, destY);
        } else {
            loadAnimInto(a, anim, x, y, std::clamp(scale, 1, 0x400), true);
            a.state = st == static_cast<int>(Actor::State::Standing) ? Actor::State::Standing : Actor::State::Animating;
            a.standName = stand;
            if (a.frames != nullptr && frame > 0 && frame < static_cast<int>(a.frames->size())) {
                const AnimFrame& f = (*a.frames)[static_cast<std::size_t>(frame)];
                a.frame = frame;
                a.pictureName = f.picture;
                try {
                    a.sprite.bitmap = session_.assets.picture(f.picture, a.scale);
                } catch (const FormatError&) {
                    a.sprite.bitmap.reset();
                }
                a.sprite.x = a.x + f.x;
                a.sprite.y = a.y + f.y;
                a.sprite.depth = depthOfRow(a.y + f.y) + f.depth;
            }
        }
        a.sprite.mode = static_cast<SpriteMode>(std::clamp(mode, 0, 3));
        a.notifyFinish = notify;
        a.finishEvent = finish;
        a.dir = dir;
    }
    events_.clear();
    const int events = r.u8();
    for (int n = 0; n < events; ++n) {
        Event e;
        e.action = r.u8();
        e.b1 = r.u8();
        e.b2 = r.u8();
        e.b3 = r.u8();
        events_.push_back(e);
    }
}

void Mission::applyRoomChange() {
    if (pendingChange_) {
        const RoomChange change = *pendingChange_;
        pendingChange_.reset();
        setUpRoom(change.room);
        spawnCrew(change.spawn);
    }
}

MissionResult playAway(Session& session, const std::string& mission, int room, int spawn) {
    MissionResult result;
    {
        Mission m(session, mission);
        result = m.run(room, spawn);
    }
    // In the whole game, the game loads saved games itself.
    while (!session.saveVoyage && result.loadSave >= 0 && !result.quit) {
        const int number = result.loadSave;
        const auto data = readSave(session.platform.saveFolder, "anniversary", number);
        if (!data) {
            result = MissionResult{};
            result.quit = true;
            break;
        }
        SaveReader r(*data);
        if (r.text() != "away") {
            throw FormatError("a saved game of a part Helmsman does not play yet");
        }
        session.restore(r);
        if (r.u8() != 0) {
            throw FormatError("a saved game of the whole game, not of one mission");
        }
        Mission m(session, mission);
        result = m.resume(r);
    }
    return result;
}

} // namespace helmsman::away
