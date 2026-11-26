// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the code of the second game's rooms and missions asks the game to
// do: the room commands (docs/rules/sequel-rooms.md has the list).
#include "game_impl.hpp"

namespace helmsman::sequel::detail {

namespace {

namespace rec {
constexpr std::uint16_t kX = 0x0C;
constexpr std::uint16_t kY = 0x0E;
constexpr std::uint16_t kTeam = 0x45;
constexpr std::uint16_t kHeld = 0x46;
constexpr std::uint16_t kLetter = 0x48;
constexpr std::uint16_t kFacing = 0x49;
constexpr std::uint16_t kNextFacing = 0x4A;
constexpr std::uint16_t kName = 0x4B;
constexpr std::uint16_t kTextHidden = 0x62;
constexpr std::uint16_t kTextSet = 0x63;
constexpr std::uint16_t kTextX = 0x64;
constexpr std::uint16_t kTextY = 0x66;
constexpr std::uint16_t kTextExtra = 0x68;
} // namespace rec

constexpr std::uint16_t kItems = 0x0F5A;
constexpr std::uint16_t kNotHandled = 0xC21F;
constexpr std::uint16_t kQuiet = 0xC21E;
constexpr std::uint16_t kResultA = 0xC222;
constexpr std::uint16_t kResultB = 0xC224;
constexpr std::uint16_t kEventBytes = 0xC218;
constexpr std::uint8_t kWait = 0xFE; // "wait until done" in place of a code

// The four ways of facing: north, south, east, west.
constexpr char kFacings[] = {'n', 's', 'e', 'w'};

std::uint32_t pack(int lo, int hi) {
    return static_cast<std::uint16_t>(lo) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(hi)) << 16);
}

} // namespace

std::uint32_t Game::roomCommand(ScriptCall& c) {
    const auto seg = currentSeg_;
    auto who = [&](int i) { return c.sarg(i); };
    auto valid = [](int n) { return n >= 0 && n < 32; };
    if (p_.devTrace) {
        std::fprintf(stderr, "  room %02x (%d %d %d %d %d)\n", c.function, c.sarg(0), c.sarg(1), c.sarg(2), c.sarg(3), c.sarg(4));
    }
    switch (c.function) {
    case 0x00: // stand
        if (valid(who(0))) {
            stand(who(0));
        }
        return 0;
    case 0x01: { // an animation: person (-1 a new one), name, x, y, code
        const std::string name = roomText(c.arg(1), 9);
        if (!name.empty() && static_cast<std::uint8_t>(name[0]) == 0xFF) {
            return 0;
        }
        const int n = startAnim(who(0), name, c.sarg(2), c.sarg(3), -1);
        if (n < 0) {
            return 0;
        }
        const auto code = static_cast<std::uint8_t>(c.arg(4));
        if (code == kWait) {
            waitDone(n);
        } else {
            setRec8(n, 0x44, code);
        }
        return static_cast<std::uint32_t>(n);
    }
    case 0x02: // the letter a person's pictures start with
        if (valid(who(0))) {
            setRec8(who(0), rec::kLetter, static_cast<std::uint8_t>(c.arg(1)));
        }
        return 0;
    case 0x03: { // how a person's lines look: x, y, colour, extra, name
        if (!valid(who(0))) {
            return 0;
        }
        const auto at = c.arg(1);
        const auto r = record(who(0));
        m_.set16(static_cast<std::uint16_t>(r + rec::kTextX), m_.cpu.read16(seg, at));
        m_.set16(static_cast<std::uint16_t>(r + rec::kTextY), m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 2)));
        m_.set8(static_cast<std::uint16_t>(r + rec::kTextSet), m_.cpu.read8(seg, static_cast<std::uint16_t>(at + 4)));
        m_.set16(static_cast<std::uint16_t>(r + rec::kTextExtra), m_.cpu.read8(seg, static_cast<std::uint16_t>(at + 5)));
        m_.setText(static_cast<std::uint16_t>(r + rec::kName), m_.scriptText(seg, static_cast<std::uint16_t>(at + 6), 23), 24);
        m_.set8(static_cast<std::uint16_t>(r + rec::kTextHidden), 0);
        return 0;
    }
    case 0x04: // a line or a choice
        return static_cast<std::uint32_t>(sayRoom(c.arg(0)));
    case 0x05: // the team gets an item
        m_.set8(static_cast<std::uint16_t>(kItems + 10 * (c.arg(0) & 0x7F)), 1);
        return 0;
    case 0x06: // to another room
        if (busy_) {
            return 0;
        }
        throw ChangeRoom{c.sarg(0), c.sarg(1)};
    case 0x07: // the team loses an item
        m_.set8(static_cast<std::uint16_t>(kItems + 10 * (c.arg(0) & 0x7F)), 0);
        if (action_ == 2 && subject_ == c.sarg(0)) {
            subject_ = -1;
            action_ = 1;
        }
        return 0;
    case 0x08: { // walk: person, x, y, code
        const int n = who(0);
        const auto code = static_cast<std::uint8_t>(c.arg(3));
        walk(n, -1, -1, c.sarg(1), c.sarg(2), code == kWait ? 0 : code);
        if (code == kWait) {
            waitDone(n);
        }
        return 0;
    }
    case 0x09: // a new walking map
        loadWalkMap(roomText(c.arg(0), 9));
        return 0;
    case 0x0B: { // a line from (x1, y1) to (x2, y2): colour, depth
        int slot = -1;
        for (int i = 0; i < 6; ++i) {
            if (!beams_[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return 0xFFFF;
        }
        const int x1 = c.sarg(0);
        const int y1 = c.sarg(1);
        const int x2 = c.sarg(2);
        const int y2 = c.sarg(3);
        const int kind = std::clamp<int>(c.sarg(4), 0, 1);
        int depth = c.sarg(5);
        if (depth < 0 || depth > 15) {
            depth = 5;
        }
        const int left = std::min(x1, x2) - 1;
        const int top = std::min(y1, y2) - 1;
        const int w = std::abs(x2 - x1) + 3;
        const int h = std::abs(y2 - y1) + 3;
        if (w > 640 || h > 400) {
            return 0xFFFF;
        }
        auto bm = std::make_shared<Bitmap>();
        bm->pixels = Surface(w, h);
        const auto colour = m_.u8(static_cast<std::uint16_t>(0x1534 + 2 * kind));
        const int n = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
        for (int i = 0; i <= n; ++i) {
            const int x = x1 + (n == 0 ? 0 : (x2 - x1) * i / n) - left;
            const int y = y1 + (n == 0 ? 0 : (y2 - y1) * i / n) - top;
            bm->pixels.set(x, y, colour);
        }
        Beam& b = beams_[slot];
        b.used = true;
        b.bitmap = bm;
        b.sprite = away::Sprite{};
        b.sprite.bitmap = bm;
        b.sprite.x = left;
        b.sprite.y = top;
        b.sprite.depth = depth;
        b.sprite.order = 100 + slot;
        return static_cast<std::uint32_t>(slot);
    }
    case 0x0C: // and away with it
        if (c.sarg(0) >= 0 && c.sarg(0) < 6) {
            beams_[c.sarg(0)] = Beam{};
        }
        return 0;
    case 0x0D: // has the team an item?
        return m_.u8(static_cast<std::uint16_t>(kItems + 10 * (c.arg(0) & 0x7F)));
    case 0x0E: { // where a person or a hotspot is
        const int n = who(0);
        if (n >= 0x20 && n < 0x40) {
            for (const auto& p : polygons(0x12, 0x14)) {
                if (p.id == n && !p.points.empty()) {
                    long long sx = 0;
                    long long sy = 0;
                    for (const auto& [x, y] : p.points) {
                        sx += x;
                        sy += y;
                    }
                    const auto k = static_cast<long long>(p.points.size());
                    return pack(static_cast<int>(sx / k), static_cast<int>(sy / k));
                }
            }
            return 0;
        }
        if (valid(n)) {
            return pack(m_.s16(static_cast<std::uint16_t>(record(n) + rec::kX)), m_.s16(static_cast<std::uint16_t>(record(n) + rec::kY)));
        }
        return 0;
    }
    case 0x0F: { // a random number from a to b
        const int lo = c.sarg(0);
        const int hi = c.sarg(1);
        return static_cast<std::uint32_t>(hi < lo ? lo : lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1)));
    }
    case 0x10: // a sound effect: (number, volume, pan)
        soundEffect(c.sarg(0), c.arg(1) & 0xFF, c.arg(2) & 0xFF);
        return 0;
    case 0x11: // music: (the song now, the one to go on with)
        music(c.sarg(0), c.sarg(1));
        return 0;
    case 0x12: { // the team beams up
        m_.set16(kResultA, c.arg(0));
        m_.set16(kResultB, c.arg(1));
        busy_ = true;
        std::vector<int> team;
        for (int i = 0; i < 32; ++i) {
            if ((rec8(i, rec::kTeam) & 1) != 0 && bodies_[i].used) {
                startAnim(i, "?teled", -1, -1, -1);
                team.push_back(i);
            }
        }
        playEffect("TRANSMAT");
        for (int i : team) {
            waitDone(i);
        }
        throw LeaveMission{4};
    }
    case 0x13: // the mission is over
        m_.set16(kResultA, c.arg(0));
        m_.set16(kResultB, c.arg(1));
        throw LeaveMission{1};
    case 0x14: // the mission is lost: load, start over or quit
        gameOverMenu();
        return 0;
    case 0x15: { // a typed answer, against a list of words
        askText();
        std::uint16_t at = c.arg(0);
        int index = 1;
        for (int guard = 0; guard < 16; ++guard) {
            const std::string word = m_.scriptText(seg, at, 30);
            if (word.empty()) {
                break;
            }
            if (lowerName(word) == lowerName(typedText_)) {
                return static_cast<std::uint32_t>(index);
            }
            at = static_cast<std::uint16_t>(at + word.size() + 1);
            ++index;
        }
        return 0;
    }
    case 0x17: { // which way a person faces
        const int n = who(0);
        if (!valid(n)) {
            return 0;
        }
        const int to = c.sarg(1);
        char f = 0;
        if (to >= 0xFA && to <= 0xFD) {
            f = kFacings[to - 0xFA];
        } else {
            int tx = 0;
            int ty = 0;
            if (valid(to)) {
                tx = m_.s16(static_cast<std::uint16_t>(record(to) + rec::kX));
                ty = m_.s16(static_cast<std::uint16_t>(record(to) + rec::kY));
            } else if (to >= 0x20 && to < 0x40) {
                ScriptCall q{c.cpu, 0x0E};
                (void)q;
                for (const auto& p : polygons(0x12, 0x14)) {
                    if (p.id == to && !p.points.empty()) {
                        tx = p.points.front().first;
                        ty = p.points.front().second;
                    }
                }
            } else {
                return 0;
            }
            const int dx = tx - m_.s16(static_cast<std::uint16_t>(record(n) + rec::kX));
            const int dy = ty - m_.s16(static_cast<std::uint16_t>(record(n) + rec::kY));
            f = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 'e' : 'w') : (dy > 0 ? 's' : 'n');
        }
        setRec8(n, rec::kNextFacing, static_cast<std::uint8_t>(f));
        return 0;
    }
    case 0x18: { // an event, handled at once
        const std::uint8_t e[4] = {static_cast<std::uint8_t>(c.arg(0)), static_cast<std::uint8_t>(c.arg(1)),
                                   static_cast<std::uint8_t>(c.arg(2)), 0};
        const bool fromScript = m_.u8(kQuiet) != 0 || c.arg(3) != 0;
        std::uint16_t saved[3];
        for (int i = 0; i < 3; ++i) {
            saved[i] = m_.u16(static_cast<std::uint16_t>(kEventBytes + 2 * i));
        }
        const bool handled = sendEvent(fromScript, true, e);
        for (int i = 0; i < 3; ++i) {
            m_.set16(static_cast<std::uint16_t>(kEventBytes + 2 * i), saved[i]);
        }
        m_.set8(kNotHandled, handled ? 0 : 1);
        return handled ? 1 : 0;
    }
    case 0x19: // more handlers, from the running code's file
        pushHandlers(currentKind_, c.arg(0), c.sarg(1));
        return 0;
    case 0x1A: // and away again
        popHandlers(currentKind_, c.arg(0));
        return 0;
    case 0x1B: // a panel of buttons: menu, x, y
        panel_ = menu(roomText(c.arg(0), 10));
        panelX_ = c.sarg(1);
        panelY_ = c.sarg(2);
        setCursor("pushbtn");
        return 0;
    case 0x1C: // and away with it
        panel_.clear();
        return 0;
    case 0x20: { // the button clicked within (lo, hi) ticks; -1 none
        if (panel_.empty()) {
            return 0xFFFF;
        }
        const std::int32_t ticks =
            c.arg(0) == 0xFFFF && c.arg(1) == 0xFFFF ? -1 : static_cast<std::int32_t>(pack(c.arg(0), c.arg(1)) & 0x7FFFFFFF);
        return static_cast<std::uint16_t>(pollPanel(ticks));
    }
    case 0x21: // the library computer
        libraryComputer();
        return 0;
    case 0x22: // held: a person stays put
        if (valid(who(0))) {
            m_.set16(static_cast<std::uint16_t>(record(who(0)) + rec::kHeld), c.arg(1));
            setRec8(who(0), rec::kTeam, static_cast<std::uint8_t>(rec8(who(0), rec::kTeam) | 2));
        }
        return 0;
    case 0x23: // gone from the room
        if (valid(who(0))) {
            const auto r = record(who(0));
            m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) & ~0x08));
            setRec8(who(0), rec::kTeam, 0);
            stand(who(0));
        }
        return 0;
    case 0x24:
        if (valid(who(0)) && (rec8(who(0), rec::kTeam) & 6) == 6) {
            m_.set16(static_cast<std::uint16_t>(record(who(0)) + rec::kHeld), 1);
        }
        return 0;
    case 0x25:
        return valid(who(0)) && (rec8(who(0), rec::kTeam) & 2) != 0 ? 1 : 0;
    case 0x26:
        return valid(who(0)) && (rec8(who(0), rec::kTeam) & 4) != 0 ? 0 : 1;
    case 0x27: // on the team
        if (valid(who(0))) {
            setRec8(who(0), rec::kTeam, 0xBD);
            const auto r = record(who(0));
            m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) | 0x08));
        }
        return 0;
    case 0x28: // off the team
        if (valid(who(0))) {
            setRec8(who(0), rec::kTeam, 0);
            const auto r = record(who(0));
            m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) & ~0x08));
        }
        return 0;
    case 0x29: { // which way a person faces, as 0xFA..0xFD
        if (!valid(who(0))) {
            return 0;
        }
        char f = static_cast<char>(rec8(who(0), rec::kNextFacing));
        if (f == 0) {
            f = static_cast<char>(rec8(who(0), rec::kFacing));
        }
        switch (f) {
        case 'n':
            return 0xFA;
        case 'e':
            return 0xFC;
        case 'w':
            return 0xFD;
        default:
            return 0xFB;
        }
    }
    case 0x2A: { // a person's team flags: those in the mask take the value
        if (!valid(who(0))) {
            return 0;
        }
        const auto mask = static_cast<std::uint8_t>(c.arg(1));
        const auto value = static_cast<std::uint8_t>(c.arg(2));
        const auto t = static_cast<std::uint8_t>((rec8(who(0), rec::kTeam) & ~mask) | (value & mask));
        setRec8(who(0), rec::kTeam, t);
        const auto r = record(who(0));
        m_.set8(r, static_cast<std::uint8_t>((m_.u8(r) & ~0x08) | (t & 0x08)));
        return 0;
    }
    case 0x2D: { // a person is stunned, falls, picks up: person, kind, code
        const int n = who(0);
        if (!valid(n)) {
            return 0;
        }
        static const char* const kKinds[] = {"?stun?", "?stun?", "?getd?", "?getu?"};
        const int kind = std::clamp<int>(c.sarg(1), 0, 3);
        const auto code = static_cast<std::uint8_t>(c.arg(2));
        if (rec8(n, rec::kLetter) != 0) {
            startAnim(n, kKinds[kind], -1, -1, -1);
        }
        if (code == kWait) {
            waitDone(n);
        } else {
            setRec8(n, 0x44, code);
            if (rec8(n, rec::kLetter) == 0) {
                finish(n);
            }
        }
        return 0;
    }
    case 0x2E: // wait
        waitAwayTicks(c.sarg(0));
        return 0;
    case 0x2F: { // the player points at something: a table of {thing, name}
        const std::uint16_t table = c.arg(0);
        const int count = std::clamp<int>(c.sarg(1), 0, 64);
        setCursor("pushbtn");
        p_.takeClicks();
        p_.keys.clear();
        int picked = -1;
        while (!p_.quit) {
            drawAway();
            const int over = thingAt(p_.mouseX, p_.mouseY);
            for (int i = 0; i < count; ++i) {
                const auto at = static_cast<std::uint16_t>(table + 4 * i);
                if (static_cast<std::int16_t>(m_.cpu.read16(seg, at)) == over) {
                    const auto name = m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 2));
                    if (name != 0) {
                        font_.drawText(p_.screen, 2, 2, m_.scriptText(seg, name, 40), 0, shade_.data());
                    }
                    break;
                }
            }
            present();
            bool chose = (p_.takeClicks() & 1) != 0;
            for (const auto& k : p_.keys) {
                chose = chose || k.scancode == scancode::kEnter;
            }
            p_.keys.clear();
            if (chose) {
                picked = over;
                break;
            }
        }
        cursorName_.clear();
        return static_cast<std::uint16_t>(picked);
    }
    case 0x30: // a scene
        playScene(p_, assets_, upperName(roomText(c.arg(0), 9)), episode_ ? &*episode_ : nullptr);
        return 0;
    case 0x31: { // a new room picture
        const std::string name = roomText(c.arg(0), 9);
        if (const auto bgd = file(name + ".BGD"); bgd && bgd->size() >= 768 + 64000) {
            std::copy_n(bgd->begin() + 768, 64000, scene_.backdrop.data());
            backdropName_ = name;
        }
        return 0;
    }
    case 0x32: { // new depths
        loadDepths(roomText(c.arg(0), 9));
        return 0;
    }
    case 0x33: // show the new picture now
        awayFrame();
        return 0;
    case 0x35: // fade in
        fadeTo(100, true);
        return 0;
    case 0x36: // fade out, then wait (animating)
        fadeTo(0, true);
        waitAwayTicks(c.sarg(0));
        return 0;
    case 0x37: // the colour fades go to (0..63 each)
        fadeColour_ = Colour{static_cast<std::uint8_t>(c.arg(0) & 0x3F), static_cast<std::uint8_t>(c.arg(1) & 0x3F),
                             static_cast<std::uint8_t>(c.arg(2) & 0x3F)};
        return 0;
    case 0x38: { // back to an episode's checkpoint: name, the code the script gets
        const std::string name = roomText(c.arg(0), 10);
        if (auto data = readCheckpoint(name)) {
            throw RestoreGame{std::move(*data), c.sarg(1)};
        }
        std::fprintf(stderr, "the checkpoint %s is missing\n", name.c_str());
        gameOverMenu();
        return 0;
    }
    case 0x39: // who speaks, with which animation
        talker_ = who(0);
        talkingAnim_ = c.arg(1) != 0 ? roomText(c.arg(1), 9) : std::string{};
        return 0;
    case 0x3A: // a sound
        playEffect(roomText(c.arg(0), 9));
        return 0;
    case 0x3B:
        p_.stopSound(SoundChannel::Effect);
        return 0;
    default:
        std::fprintf(stderr, "room command %02x (%04x %04x %04x %04x) not done yet\n", c.function, c.arg(0), c.arg(1),
                     c.arg(2), c.arg(3));
        return 0;
    }
}

} // namespace helmsman::sequel::detail
