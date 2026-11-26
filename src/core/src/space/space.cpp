// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/space/space.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace helmsman::space {

namespace {

// Where a hit lands, by the side of the ship it comes from: of a hundred
// hits, those in [from, to] strike `part`; what the part cannot take goes
// to the hull.
struct HitChance {
    int from;
    int to;
    int part;
};
constexpr HitChance kRight[] = {{0, 19, 9},  {20, 24, 8}, {25, 34, 7}, {35, 39, 1}, {40, 49, 6},
                                {50, 51, 4}, {52, 54, 5}, {55, 56, 2}, {57, 59, 3}, {60, 99, 0}};
constexpr HitChance kBehind[] = {{0, 24, 9}, {25, 49, 8}, {50, 64, 7}, {65, 99, 0}};
constexpr HitChance kLeft[] = {{0, 19, 8},  {20, 24, 9}, {25, 34, 7}, {35, 39, 1}, {40, 49, 6},
                               {50, 51, 5}, {52, 54, 4}, {55, 56, 3}, {57, 59, 2}, {60, 99, 0}};
constexpr HitChance kAhead[] = {{0, 9, 8},   {10, 19, 9}, {20, 24, 1}, {25, 39, 6}, {40, 44, 5},
                                {45, 49, 4}, {50, 54, 3}, {55, 59, 2}, {60, 99, 0}};
constexpr HitChance kAbove[] = {{0, 19, 8}, {20, 39, 9}, {40, 49, 7}, {50, 54, 1}, {55, 56, 3}, {57, 59, 2}, {60, 99, 0}};
constexpr HitChance kBelow[] = {{0, 14, 8},  {15, 29, 9}, {30, 34, 7}, {35, 44, 6}, {45, 49, 5},
                                {50, 54, 4}, {55, 57, 3}, {58, 59, 2}, {60, 99, 0}};
struct Side {
    const HitChance* chances;
    int count;
};
constexpr Side kSides[6] = {
    {kRight, 10}, {kBehind, 4}, {kLeft, 10}, {kAhead, 9}, {kAbove, 7}, {kBelow, 9},
};

// The colour of a shot, by the shooter's picture set.
std::uint8_t shotColour(int set) {
    switch (set) {
    case 0: return 0xA0;
    case 1: return 0xB8;
    case 2: return 0xEA;
    default: return 0xA0;
    }
}

std::int32_t mul16(std::int32_t whole, std::int32_t frac16) {
    // (whole part of a 16.16 number) times a 16.16 factor.
    return static_cast<std::int32_t>(static_cast<std::int64_t>(whole >> 16) * frac16);
}

std::int32_t clampAbs(std::int32_t v, std::int32_t limit) { return std::clamp(v, -limit, limit); }

int randomInt(std::mt19937& rng) { return static_cast<int>(rng() & 0x7FFF); }

} // namespace

ShipClass ShipClass::fromBytes(std::span<const std::uint8_t> bytes, int recordSize) {
    Reader r(bytes);
    ShipClass c;
    c.picture = r.s16();
    c.size = r.s16();
    c.scale = r.s16();
    const std::size_t extra = recordSize > 6 ? static_cast<std::size_t>(recordSize - 6) : 0;
    for (auto& s : c.shields) {
        s.on = r.s16() != 0;
        s.best = r.s16();
        s.now = r.s16();
        r.skip(extra);
    }
    for (auto& p : c.parts) {
        p.on = r.s16() != 0;
        p.best = r.s16();
        p.now = r.s16();
        r.skip(extra);
    }
    c.maxTurn = static_cast<std::int32_t>(r.u32());
    c.turnChange = static_cast<std::int32_t>(r.u32());
    c.maxSpeed = static_cast<std::int32_t>(r.u32());
    c.speedChange = static_cast<std::int32_t>(r.u32());
    r.skip(0x42 - 16); // the moving state, all zero in the files
    c.range = static_cast<std::int32_t>(r.u32());
    c.cloaks = r.s16() != 0;
    for (auto& m : c.mounts) {
        m.readyAt = r.u32();
        m.life = r.s16();
        m.speed = r.s16();
        m.reload = r.s16();
        m.offset.x = static_cast<std::int32_t>(r.u32());
        m.offset.y = static_cast<std::int32_t>(r.u32());
        m.offset.z = static_cast<std::int32_t>(r.u32());
        m.shot = r.s16();
    }
    return c;
}

Space::Space(const Archive& archive) : archive_(archive) {
    loadPictures();
    setView(view_, 4);
}

void Space::loadPictures() {
    // The stars: sixteen small pictures, one after the other.
    if (const auto data = archive_.load("STARS.SHP")) {
        std::size_t at = 0;
        while (at + 8 <= data->size() && starPictures_.size() < 16) {
            Reader r(std::span<const std::uint8_t>(*data).subspan(at));
            r.skip(4);
            const std::size_t w = r.u16();
            const std::size_t h = r.u16();
            const std::size_t size = 8 + w * h;
            if (at + size > data->size()) {
                break;
            }
            starPictures_.push_back(Bitmap::fromBytes(std::span<const std::uint8_t>(*data).subspan(at, size)));
            at += size;
        }
    }
}

std::shared_ptr<const Bitmap> Space::picture(const std::string& name) {
    std::string key;
    for (char c : name) {
        key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (const auto found = pictures_.find(key); found != pictures_.end()) {
        return found->second;
    }
    std::shared_ptr<const Bitmap> b;
    // Most are kept as .SHP, a few (the warp flash) as plain pictures.
    for (const char* ext : {".SHP", ".BMP"}) {
        if (const auto data = load(key + ext)) {
            try {
                b = std::make_shared<const Bitmap>(Bitmap::fromBytes(*data));
            } catch (const FormatError&) {
            }
            break;
        }
    }
    pictures_[key] = b;
    return b;
}

const ShipClass& Space::shipClass(const std::string& name) {
    std::string key;
    for (char c : name) {
        key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    auto found = classes_.find(key);
    if (found == classes_.end()) {
        const auto data = load(key + ".DSC");
        if (!data) {
            throw FormatError("the ship description " + key + ".DSC is missing");
        }
        found = classes_.emplace(key, ShipClass::fromBytes(*data, sequel_ ? 8 : 6)).first;
    }
    return found->second;
}

std::optional<std::vector<std::uint8_t>> Space::load(const std::string& name) const {
    if (auto data = archive_.load(name)) {
        return data;
    }
    if (moreFiles_) {
        return moreFiles_(name);
    }
    return std::nullopt;
}

void Space::setView(ClipRect rect, int zoom) {
    view_ = rect;
    zoom_ = std::clamp(zoom, 1, 0x100); // the bridge 4..8, the titles more
    // The original passes the corners inclusive.
    const int x1 = rect.left;
    const int y1 = rect.top;
    const int x2 = rect.right - 1;
    const int y2 = rect.bottom - 1;
    centreX_ = (x1 + x2) / 2;
    centreY_ = (y1 + y2) / 2;
    halfW_ = (x2 - x1 + 1) / 2;
    halfH_ = (y2 - y1 + 1) / 2;
    const int angle = 0x200 / zoom_;
    const auto tangent = static_cast<std::int32_t>(std::tan(angle * 2.0 * std::numbers::pi / 1024.0) * kOne);
    focal_ = std::max<std::int32_t>(1, static_cast<std::int32_t>((static_cast<std::int64_t>(halfW_) << 14) / std::max(1, tangent)));
    for (auto& s : stars_) {
        s.live = false;
    }
}

void Space::setPlanet(int seed, int ramp, const Vec& where) {
    planet_ = Object{};
    planetSurface_.clear();
    if (!planetLook_) {
        return;
    }
    planet_.kind = Kind::Planet;
    planet_.pos = where;
    planet_.axes = heading(planetSpin_);
    planetSurface_ = planetSurface(*planetLook_, seed, ramp);
}

void Space::clear() {
    for (auto& o : objects_) {
        o = Object{};
    }
    playerDead_ = false;
}

void Space::setPlayer(const std::string& cls) {
    Object& p = player();
    const Vec pos = p.pos;
    const Mat axes = p.kind == Kind::Ship ? p.axes : Mat::identity();
    p = Object{};
    p.kind = Kind::Ship;
    p.cls = shipClass(cls);
    p.className = cls;
    p.type = p.cls.picture;
    p.size = p.cls.size;
    p.scale = p.cls.scale;
    p.pos = pos;
    p.axes = axes;
    playerDead_ = false;
}

int Space::addShip(const std::string& cls) {
    for (int i = kFirstShip; i < kFirstShip + kShips; ++i) {
        Object& s = object(i);
        if (s.kind != Kind::None) {
            continue;
        }
        s = Object{};
        s.kind = Kind::Ship;
        s.cls = shipClass(cls);
        s.className = cls;
        s.type = s.cls.picture;
        s.size = s.cls.size;
        s.scale = s.cls.scale;
        auto scatter = [this] { return (0x40 - (randomInt(rng_) & 0x7F)) * 16; };
        s.pos = player().pos;
        s.pos.x += scatter();
        s.pos.y += scatter();
        s.pos.z += scatter();
        s.axes = Mat::identity();
        s.shieldsUp = true;
        s.weaponsArmed = true;
        s.cloakLevel = 1;
        s.behaviour = 0;
        return i;
    }
    return -1;
}

int Space::addStill(const std::string& name, Vec where) {
    for (int i = kFirstStill; i < kFirstStill + kStills; ++i) {
        Object& s = object(i);
        if (s.kind != Kind::None) {
            continue;
        }
        s = Object{};
        s.kind = Kind::Still;
        s.stillName = name;
        s.size = 150;
        s.scale = 3;
        s.pos = player().pos + applyTransposed(player().axes, where);
        return i;
    }
    return -1;
}

void Space::remove(int index) { object(index) = Object{}; }

namespace {

void savePart(SaveWriter& w, const Part& p) {
    w.u8(p.on ? 1 : 0);
    w.s32(p.best);
    w.s32(p.now);
}

void restorePart(SaveReader& r, Part& p) {
    p.on = r.u8() != 0;
    p.best = r.s32();
    p.now = r.s32();
}

void saveVec(SaveWriter& w, const Vec& v) {
    w.s32(v.x);
    w.s32(v.y);
    w.s32(v.z);
}

Vec restoreVec(SaveReader& r) {
    Vec v;
    v.x = r.s32();
    v.y = r.s32();
    v.z = r.s32();
    return v;
}

} // namespace

void Space::save(SaveWriter& w) const {
    for (int i = 0; i < kObjects; ++i) {
        const Object& o = object(i);
        const bool kept = (o.kind == Kind::Ship && !o.destroyed) || o.kind == Kind::Still;
        if (!kept) {
            w.u8(0);
            continue;
        }
        w.u8(static_cast<std::uint8_t>(o.kind));
        w.text(o.kind == Kind::Ship ? o.className : o.stillName);
        saveVec(w, o.pos);
        for (const std::int16_t m : o.axes.m) {
            w.u16(static_cast<std::uint16_t>(m));
        }
        if (o.kind != Kind::Ship) {
            continue;
        }
        for (const Part& p : o.cls.parts) {
            savePart(w, p);
        }
        for (const Part& p : o.cls.shields) {
            savePart(w, p);
        }
        w.s32(o.wantSpeed);
        w.u8(o.shieldsUp ? 1 : 0);
        w.u8(o.weaponsArmed ? 1 : 0);
        w.u8(static_cast<std::uint8_t>(o.behaviour));
    }
}

void Space::restore(SaveReader& r) {
    for (int i = 0; i < kObjects; ++i) {
        Object& o = object(i);
        const int kind = r.u8();
        o = Object{};
        if (kind == 0) {
            continue;
        }
        if (kind != static_cast<int>(Kind::Ship) && kind != static_cast<int>(Kind::Still)) {
            throw FormatError("a saved object of an unknown kind");
        }
        const std::string name = r.text();
        o.kind = static_cast<Kind>(kind);
        if (o.kind == Kind::Ship) {
            if (name.empty() || name.size() > 12) {
                throw FormatError("a saved ship without a proper class");
            }
            o.cls = shipClass(name);
            o.className = name;
            o.type = o.cls.picture;
            o.size = o.cls.size;
            o.scale = o.cls.scale;
        } else {
            o.stillName = name;
            o.size = 150;
            o.scale = 3;
        }
        o.pos = restoreVec(r);
        for (std::int16_t& m : o.axes.m) {
            m = static_cast<std::int16_t>(r.u16());
        }
        if (o.kind != Kind::Ship) {
            continue;
        }
        for (Part& p : o.cls.parts) {
            restorePart(r, p);
        }
        for (Part& p : o.cls.shields) {
            restorePart(r, p);
        }
        o.wantSpeed = r.s32();
        o.shieldsUp = r.u8() != 0;
        o.weaponsArmed = r.u8() != 0;
        o.behaviour = r.u8();
    }
    if (player().kind != Kind::Ship) {
        throw FormatError("a saved game without the player's ship");
    }
    playerDead_ = false;
}

bool Space::enemiesLeft() const {
    for (int i = kFirstShip; i < kFirstShip + kShips; ++i) {
        if (object(i).kind == Kind::Ship) {
            return true;
        }
    }
    return false;
}

// --- The player's controls -------------------------------------------------

void Space::steer(int dx, int dy) {
    Object& p = player();
    p.wantTurn[1] = dx * 65536;
    p.wantTurn[0] = dy * 65536;
}

void Space::setRoll(int direction) { player().wantTurn[2] = direction == 0 ? 0 : (direction > 0 ? 1 : -1) * player().cls.maxTurn; }

void Space::setSpeedLevel(int level) {
    Object& p = player();
    p.wantSpeed = static_cast<std::int32_t>(static_cast<std::int64_t>(std::clamp(level, 0, 9)) * p.cls.maxSpeed / 9);
}

void Space::fullReverse() { player().wantSpeed = -(player().cls.maxSpeed >> 3); }

bool Space::fireTorpedoes(int sx, int sy) {
    const Vec aimView{sx - centreX_, centreY_ - sy, focal_};
    const Vec aim = applyTransposed(player().axes, aimView);
    bool any = fire(kPlayer, 0, &aim);
    any = fire(kPlayer, 1, &aim) || any;
    if (any && sound) {
        sound(0x25);
    }
    return any;
}

bool Space::firePhasers(int sx, int sy) {
    const Vec aimView{sx - centreX_, centreY_ - sy, focal_};
    const Vec aim = applyTransposed(player().axes, aimView);
    bool any = fire(kPlayer, 2, &aim);
    any = fire(kPlayer, 3, &aim) || any;
    if (any && sound) {
        sound(0x24);
    }
    return any;
}

bool Space::ahead(int index) const {
    const Object& s = object(index);
    if (s.kind != Kind::Ship || s.destroyed) {
        return false;
    }
    const Object& me = object(kPlayer);
    const Vec local = apply(me.axes, s.pos - me.pos);
    return local.z > 0 && std::llabs(local.x) <= local.z && std::llabs(local.y) <= local.z;
}

bool Space::fireAt(int index, bool torpedoes) {
    const Object& s = object(index);
    if (s.kind != Kind::Ship) {
        return false;
    }
    const Vec aim = s.pos - player().pos;
    const int first = torpedoes ? 0 : 2;
    bool any = fire(kPlayer, first, &aim);
    any = fire(kPlayer, first + 1, &aim) || any;
    if (any && sound) {
        sound(torpedoes ? 0x25 : 0x24);
    }
    return any;
}

// --- Ships -----------------------------------------------------------------

void Space::power(Object& s, bool isPlayer) {
    auto& parts = s.cls.parts;
    s.power = parts[kPortWarp].now + parts[kStarboardWarp].now + parts[kImpulse].now;
    if (isPlayer && s.powerBoost > 0) {
        s.power += 10;
        --s.powerBoost;
    }
    if (s.shieldsUp) {
        if (s.power >= 10) {
            s.power -= 10;
            s.shieldsWork = true;
        } else if (s.shieldsWork) {
            if (report) report(2, 0, isPlayer);
            s.shieldsWork = false;
        }
    } else {
        s.shieldsWork = false;
    }
    if (s.wantsCloak) {
        if (s.power >= 20) {
            if (!s.cloaked) {
                s.cloakLevel = 0;
            }
            s.power -= 20;
            s.cloaked = true;
        } else {
            s.cloaked = false;
        }
    } else {
        s.cloaked = false;
    }
    if (s.weaponsArmed) {
        if (s.power >= 10) {
            s.power -= 10;
            s.weaponsWork = true;
        } else if (s.weaponsWork) {
            if (report) report(3, 0, isPlayer);
            s.weaponsWork = false;
        }
    } else {
        s.weaponsWork = false;
    }
    const int available = std::min(s.power, 70);
    const std::int32_t maxSpeed = std::max<std::int32_t>(1, s.cls.maxSpeed);
    const auto needed = static_cast<int>(static_cast<std::int64_t>(s.wantSpeed) * 70 / maxSpeed);
    if (needed > available) {
        s.allowedSpeed = static_cast<std::int32_t>(static_cast<std::int64_t>(maxSpeed) * available / 70);
        s.power -= available;
    } else {
        s.allowedSpeed = s.wantSpeed;
        s.power -= needed;
    }
}

void Space::fly(Object& s) {
    if (s.kind == Kind::Shot) {
        s.pos = s.pos + s.velocity;
        return;
    }
    const Part& bridge = s.cls.parts[kBridge];
    const std::int32_t factor =
        static_cast<std::int32_t>((static_cast<std::int64_t>(bridge.now) << 16) / std::max(1, bridge.best));
    for (int axis = 0; axis < 3; ++axis) {
        std::int32_t d = s.wantTurn[axis] - s.turnRate[axis];
        if (d != 0) {
            d = clampAbs(d, s.cls.turnChange);
            d = mul16(d, factor);
            s.turnRate[axis] = clampAbs(s.turnRate[axis] + d, s.cls.maxTurn);
        }
    }
    const int steps[3] = {s.turnRate[0] >> 16, s.turnRate[1] >> 16, s.turnRate[2] >> 16};
    if (steps[0] != 0) {
        pitch(s.axes, steps[0]);
        s.turned[0] += stepAngle(steps[0]);
    }
    if (steps[1] != 0) {
        yaw(s.axes, steps[1]);
        s.turned[1] += stepAngle(steps[1]);
    }
    if (steps[2] != 0) {
        roll(s.axes, steps[2]);
        s.turned[2] += stepAngle(steps[2]);
    }
    --s.timer;
    // Speed comes up to what is allowed a little at a time.
    s.speed += clampAbs(s.allowedSpeed - s.speed, s.cls.speedChange);
    const std::int32_t whole = s.speed >> 16;
    const Vec fwd = s.axes.row(2);
    s.velocity = {(whole * fwd.x) >> 14, (whole * fwd.y) >> 14, (whole * fwd.z) >> 14};
    s.pos = s.pos + s.velocity;
}

void Space::repair(Object& s, bool isPlayer) {
    if ((ticks_ & 31) != 7) {
        return;
    }
    auto mend = [](Part& p) { p.now = std::min(p.now + 1, p.best); };
    for (auto& sh : s.cls.shields) mend(sh);
    for (auto& p : s.cls.parts) mend(p);
    if (!isPlayer || repairFirst < 0) {
        return;
    }
    auto group = [&](std::span<Part> parts) {
        bool done = true;
        for (auto& p : parts) {
            if (p.now != p.best) {
                mend(p);
                done = done && p.now == p.best;
            }
        }
        return done;
    };
    bool done = false;
    auto& parts = s.cls.parts;
    switch (repairFirst) {
    case kHull:
    case kBridge:
    case kSensors: {
        Part& p = parts[static_cast<std::size_t>(repairFirst)];
        done = p.now == p.best;
        mend(p);
        break;
    }
    case kPortTorpedoes: done = group(std::span<Part>(&parts[2], 2)); break;
    case kPortPhasers: done = group(std::span<Part>(&parts[4], 2)); break;
    case kImpulse: done = group(std::span<Part>(&parts[7], 3)); break;
    case 0x7FFF: done = group(std::span<Part>(s.cls.shields.data(), 6)); break;
    default: return;
    }
    if (done) {
        if (report) report(7, repairFirst, true);
        repairFirst = -1;
    }
}

int Space::sideHit(const Vec& v) const {
    const std::int64_t ax = std::llabs(v.x);
    const std::int64_t ay = std::llabs(v.y);
    const std::int64_t az = std::llabs(v.z);
    if (ax >= ay && ax >= az) {
        return v.x < 0 ? 2 : 0;
    }
    if (ay >= az) {
        return v.y < 0 ? 5 : 4;
    }
    return v.z < 0 ? 1 : 3;
}

bool Space::damagePart(Object& s, int side, int damage) {
    const Side& table = kSides[side];
    const int roll = randomInt(rng_) % 100;
    for (int i = 0; i < table.count; ++i) {
        const HitChance& h = table.chances[i];
        if (roll < h.from || roll > h.to) {
            continue;
        }
        Part& p = s.cls.parts[static_cast<std::size_t>(h.part)];
        const int take = std::min(p.now, damage);
        p.now -= take;
        damage -= take;
        if (damage == 0) {
            return false;
        }
        s.cls.parts[kHull].now -= damage;
        return s.cls.parts[kHull].now <= 0;
    }
    return false;
}

void Space::hit(int target, int shotIndex) {
    Object& t = object(target);
    Object& shot = object(shotIndex);
    const Vec local = apply(t.axes, shot.pos - t.pos);
    const int side = sideHit(local);
    int damage = shot.damage;
    if (t.shieldsWork) {
        Part& sh = t.cls.shields[static_cast<std::size_t>(side)];
        const int before = sh.now;
        sh.now -= std::min(before, damage);
        damage -= damage * before / std::max(1, sh.best);
    }
    if (damagePart(t, side, damage) && !t.destroyed) {
        if (target == kPlayer) {
            if (music) music(4);
        } else if (music) {
            music(3);
        }
        t.destroyed = true;
        t.blastFrame = -1;
        t.wantsCloak = false;
        t.cloaked = false;
    }
}

bool Space::fire(int shooter, int mountIndex, const Vec* aim) {
    Object& s = object(shooter);
    Mount& m = s.cls.mounts[static_cast<std::size_t>(mountIndex)];
    const Part& part = s.cls.parts[static_cast<std::size_t>(kPortTorpedoes + mountIndex)];
    if (part.now < part.best / 2 || m.readyAt > ticks_ || !part.on || !s.weaponsWork) {
        return false;
    }
    for (int i = kFirstShot; i < kFirstShot + kShots; ++i) {
        Object& shot = object(i);
        if (shot.kind != Kind::None) {
            continue;
        }
        shot = Object{};
        shot.pos = s.pos + applyTransposed(s.axes, m.offset);
        if (aim == nullptr) {
            shot.axes = s.axes;
        } else {
            shot.axes = Mat{};
            const Vec f = unit(*aim);
            shot.axes.at(2, 0) = static_cast<std::int16_t>(std::clamp(f.x, -32768, 32767));
            shot.axes.at(2, 1) = static_cast<std::int16_t>(std::clamp(f.y, -32768, 32767));
            shot.axes.at(2, 2) = static_cast<std::int16_t>(std::clamp(f.z, -32768, 32767));
        }
        shot.kind = Kind::Shot;
        shot.type = m.shot;
        shot.size = 60;
        shot.scale = 3;
        shot.speed = (m.speed << 16) + s.speed;
        const std::int32_t whole = shot.speed >> 16;
        const Vec fwd = shot.axes.row(2);
        shot.velocity = {(whole * fwd.x) >> 14, (whole * fwd.y) >> 14, (whole * fwd.z) >> 14};
        shot.owner = shooter;
        shot.life = m.life;
        shot.damage = part.best;
        shot.colour = shotColour(s.type);
        m.readyAt = static_cast<std::uint32_t>(ticks_ + static_cast<std::uint64_t>(std::max(0, m.reload)));
        return true;
    }
    return false;
}

void Space::aimAndFire(int index, const Vec& local, const Vec& toTarget) {
    const std::int64_t ax = std::llabs(local.x);
    const std::int64_t ay = std::llabs(local.y);
    if (std::max(ax, ay) * 4 > local.z) {
        return;
    }
    bool any = false;
    for (int m = 0; m < 4; ++m) {
        any = fire(index, m, &toTarget) || any;
    }
    if (any && sound) {
        sound(0x25);
    }
}

void Space::runManoeuvre(int index, const Vec&, std::int32_t) {
    Object& s = object(index);
    auto either = [this](std::int32_t v) { return (rng_() & 1) ? -v : v; };
    auto stop = [&] {
        s.wantTurn[0] = s.wantTurn[1] = s.wantTurn[2] = 0;
    };
    auto movingAwayFar = [&] { return s.closing > 0 && s.distance > s.cls.range; };
    const std::int32_t half = 0x200 << 16;
    const std::int32_t quarter = 0x100 << 16;
    const std::int32_t whole = 0x400 << 16;
    int& step = s.manoeuvreStep;
    switch (s.manoeuvre) {
    case 0: // a loop: up and over
        if (step == 0) {
            s.wantSpeed = s.cls.maxSpeed;
            stop();
            s.wantTurn[0] = either(s.cls.maxTurn);
            s.turned[0] = 0;
            step = 1;
        } else if (step == 1) {
            if (s.turned[0] >= half) {
                if (s.closing > 0) {
                    s.manoeuvre = -1;
                }
                step = 2;
                return;
            }
        } else if (s.turned[0] >= whole) {
            s.manoeuvre = -1;
            return;
        }
        fly(s);
        break;
    case 1: // straight on, then a turn about and back
        switch (step) {
        case 0:
            s.wantSpeed = s.cls.maxSpeed;
            stop();
            s.timer = 0x20;
            step = 1;
            break;
        case 1:
        case 5:
        case 7:
            if (s.timer < 0) {
                s.wantTurn[0] = -s.cls.maxTurn;
                s.turned[0] = 0;
                ++step;
            }
            break;
        case 2:
        case 6:
            if (s.turned[0] >= quarter) {
                s.wantTurn[0] = 0;
                s.timer = 0x30;
                ++step;
            }
            break;
        case 3:
            if (s.timer < 0) {
                s.wantTurn[1] = either(s.cls.maxTurn);
                s.turned[1] = 0;
                ++step;
            }
            break;
        case 4:
            if (s.turned[1] >= half) {
                s.wantTurn[1] = 0;
                s.timer = 0x30;
                ++step;
            }
            break;
        default:
            s.manoeuvre = -1;
            return;
        }
        fly(s);
        break;
    default: { // jinking: short random turns
        const int pair = step & ~1;
        if ((step & 1) == 0) {
            if (movingAwayFar()) {
                s.manoeuvre = -1;
                fly(s);
                break;
            }
            s.wantSpeed = s.cls.maxSpeed;
            if (pair == 0) {
                stop();
                s.timer = static_cast<int>(rng_() & 0x3F);
            } else {
                const int axis = pair / 2 - 1; // 2 pitch, 4 yaw, 6 roll
                stop();
                s.wantTurn[axis] = either(s.cls.maxTurn);
                s.turned[axis] = 0;
            }
            ++step;
        } else if (pair == 0) {
            if (s.timer < 0) {
                step = static_cast<int>(rng_() & 3) * 2;
            }
        } else {
            const int axis = pair / 2 - 1;
            if (s.turned[axis] >= quarter) {
                step = static_cast<int>(rng_() & 3) * 2;
            }
        }
        fly(s);
        break;
    }
    }
}

void Space::think(int index) {
    Object& s = object(index);
    if (s.destroyed) {
        fly(s);
        return;
    }
    power(s, false);
    repair(s, false);
    if (s.cls.cloaks) {
        s.wantsCloak = true;
    }
    const Part& sensors = s.cls.parts[kSensors];
    const int dull = sensors.best * 3 / 4;
    if (dull > 0 && randomInt(rng_) % dull > sensors.now) {
        fly(s);
        return;
    }
    const Vec toTarget = player().pos - s.pos;
    const Vec local = apply(s.axes, toTarget);
    const std::int32_t dist = roughLength(local);
    s.closing = dist - s.distance;
    s.distance = dist;
    if (s.cls.cloaks) {
        const std::int64_t m = std::max(std::llabs(local.x), std::llabs(local.y));
        if (m * 4 <= local.z) {
            s.wantsCloak = false;
        }
        if (s.cloakLevel == 0) {
            aimAndFire(index, local, toTarget);
        }
    } else {
        aimAndFire(index, local, toTarget);
    }
    if (s.behaviour == 1) {
        if (s.distance > 12000) {
            remove(index);
            return;
        }
        // Turn away from the player and run.
        const double d = std::max<std::int32_t>(1, dist);
        const int up = static_cast<int>(std::asin(std::clamp(local.y / d, -1.0, 1.0)) / (2 * std::numbers::pi) * kTurn);
        int side = angleOf(local.x, local.z);
        if (side >= kTurn / 2) side -= kTurn;
        s.wantSpeed = s.cls.maxSpeed;
        s.wantTurn[0] = up << 16;
        s.wantTurn[1] = -side * 65536;
        s.wantTurn[2] = 0;
        fly(s);
        return;
    }
    std::int32_t speed = 0;
    if (s.allowedSpeed < (s.wantSpeed >> 2)) {
        s.manoeuvre = -1;
    } else if (s.manoeuvre >= 0) {
        runManoeuvre(index, local, dist);
        return;
    } else if (s.distance <= s.cls.range) {
        s.manoeuvre = static_cast<int>(rng_() % 3);
        s.manoeuvreStep = 0;
        return;
    } else if (s.distance > (s.cls.range << 2) || s.distance > s.cls.range * 3) {
        speed = s.cls.maxSpeed;
    } else if (s.distance > (s.cls.range << 1)) {
        speed = s.cls.maxSpeed >> 1;
    } else {
        speed = s.cls.maxSpeed >> 2;
    }
    // Turn towards the player.
    const double d = std::max<std::int32_t>(1, dist);
    const int up = static_cast<int>(std::asin(std::clamp(local.y / d, -1.0, 1.0)) / (2 * std::numbers::pi) * kTurn);
    int side = angleOf(local.x, local.z);
    if (side >= kTurn / 2) side -= kTurn;
    s.wantSpeed = speed;
    s.wantTurn[0] = -up * 65536;
    s.wantTurn[1] = side << 16;
    s.wantTurn[2] = 0;
    fly(s);
}

void Space::updateShots() {
    for (int i = kFirstShot; i < kFirstShot + kShots; ++i) {
        Object& shot = object(i);
        if (shot.kind != Kind::Shot) {
            continue;
        }
        if (shot.life-- <= 0) {
            remove(i);
            continue;
        }
        if (shot.type != kBlast) {
            for (int t = kPlayer; t < kFirstShip + kShips; ++t) {
                Object& target = object(t);
                if (target.kind != Kind::Ship || t == shot.owner) {
                    continue;
                }
                const Vec d = shot.pos - target.pos;
                const std::int32_t r = target.size;
                if (d.x <= -r || d.x >= r || d.y <= -r || d.y >= r || d.z <= -r || d.z >= r) {
                    continue;
                }
                if ((shot.owner == kPlayer) == (t == kPlayer)) {
                    break;
                }
                hit(t, i);
                shot.type = kBlast;
                shot.life = 8;
                shot.size = 60;
                shot.scale = 6;
                shot.axes = target.axes;
                shot.velocity = target.velocity;
                if (t != kPlayer) {
                    lastHit = t;
                }
                break;
            }
        }
        fly(shot);
    }
}

void Space::tick() {
    ++ticks_;
    // The planet turns, a little each tick.
    planetSpin_ = (planetSpin_ + 4) & (kTurn - 1);
    if (planet_.kind == Kind::Planet) {
        planet_.axes = heading(planetSpin_);
    }
    Object& p = player();
    if (p.kind == Kind::Ship && !p.destroyed) {
        power(p, true);
        fly(p);
        repair(p, true);
    } else if (p.kind == Kind::Ship) {
        fly(p);
    }
    for (int i = kFirstShip; i < kFirstShip + kShips; ++i) {
        if (object(i).kind == Kind::Ship) {
            think(i);
        }
    }
    updateShots();
    if ((ticks_ & 63) == 0) {
        for (auto& o : objects_) {
            if (o.kind == Kind::Ship || o.kind == Kind::Still) {
                tidy(o.axes);
            }
        }
    }
    const Part& sensors = p.cls.parts[kSensors];
    static_ = sensors.best > 0 && randomInt(rng_) % sensors.best >= sensors.now && sensors.now < sensors.best;
}

} // namespace helmsman::space
