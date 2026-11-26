// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/bridge/bridge.hpp"

#include "bridge/missions.hpp"

#include "helmsman/away/menus.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>

namespace helmsman::bridge {

namespace {

// The viewscreen, on the screen.
constexpr int kViewLeft = 0x48;
constexpr int kViewTop = 0x1E;
constexpr int kViewRight = 0xF7;
constexpr int kViewBottom = 0x66;

// Each officer's menu and where it opens.
struct OfficerMenu {
    const char* menu;
    int x;
    int y;
};
constexpr OfficerMenu kMenus[8] = {
    {"command", 60, 100}, {"comp", 200, 30}, {"", 0, 0},   {"", 0, 0},
    {"helm", 60, 70},     {"nav", 60, 70},   {"comm", 30, 30}, {"eng", 30, 30},
};

struct Speaker {
    const char* name;
    std::uint8_t colour;
    int x;
    int y;
};
// Who speaks, in what colour, and where their boxes open.
constexpr Speaker kSpeakers[8] = {
    {"Captain Kirk", 0xB0, 0xA0, 0x82}, {"Mr. Spock", 0x2C, 0x126, 0x6A}, {"Dr. McCoy", 0x2C, 0xA0, 0x64},
    {"Security Officer", 0xB0, 0x7A, 0x74}, {"Mr. Sulu", 0xB0, 0x7A, 0x74}, {"Mr. Chekov", 0xB0, 0xC4, 0x74},
    {"Lieutenant Uhura", 0xA1, 0x12A, 0x96}, {"Mr. Scott", 0xA1, 0x40, 0x64},
};

// What Scott calls each system when it is repaired.
struct Repaired {
    int system;
    const char* name;
};
constexpr Repaired kRepairedNames[7] = {
    {0xFF, "#BRID\\SCOTSHIE#Shields"},   {0, "#BRID\\SCOTHULL#Hull"},       {1, "#BRID\\SCOTBRID#Bridge"},
    {6, "#BRID\\SCOTSENS#Sensors"},      {2, "#BRID\\SCOTTORP#Torpedoes"},  {4, "#BRID\\SCOTPHAS#Phasers"},
    {7, "#BRID\\SCOTENGI#Engines"},
};

// The star systems on the star map, by number: where each is drawn.
constexpr int kSystems[45][2] = {
    {14, 16},   {28, 59},   {62, 32},   {102, 38},  {116, 33},  {167, 18},  {245, 27},  {263, 8},   {294, 37},
    {260, 63},  {253, 118}, {230, 122}, {237, 93},  {220, 77},  {177, 62},  {167, 111}, {118, 79},  {90, 102},
    {73, 111},  {111, 124}, {21, 95},   {49, 28},   {81, 153},  {40, 133},  {25, 121},  {112, 159}, {154, 169},
    {160, 152}, {184, 135}, {213, 166}, {271, 165}, {269, 136}, {297, 145}, {303, 103}, {279, 111}, {280, 85},
    {300, 14},  {217, 33},  {218, 55},  {166, 85},  {50, 44},   {151, 44},  {156, 16},  {83, 72},   {150, 102},
};

int randomInt(int below) {
    static std::mt19937 rng{std::random_device{}()};
    return below <= 1 ? 0 : static_cast<int>(rng() % static_cast<unsigned>(below));
}

// The game's sound effects by number.
const char* soundName(int number) {
    switch (number) {
    case 0x04: return "TRICORDE";
    case 0x05: return "STDOOR1";
    case 0x06: return "PHASSHOT";
    case 0x08: return "TRANSDEM";
    case 0x09: return "TRANSMAT";
    case 0x0A: return "TRANSENE";
    case 0x22: return "HAILING";
    case 0x24: return "PHASSHOT";
    case 0x25: return "PHOTSHOT";
    case 0x26: return "HITSHIEL";
    case 0x28: return "REDALERT";
    case 0x29: return "WARP";
    default: return nullptr;
    }
}

// The words for a number the computer says: its voice clips, each
// preceded by a comma, as a text's voice list wants them.
std::string numberVoices(int n) {
    int tens = 0;
    int units = n;
    if (n >= 20 && n <= 99) {
        tens = 0xF8 + (n / 10 - 2);
        units = n % 10;
    } else if (n == 100) {
        tens = 0x100;
        units = 0;
    }
    std::string out;
    char part[24];
    if (tens != 0) {
        std::snprintf(part, sizeof part, ",brid\\b_%03d", tens);
        out += part;
    }
    if (tens == 0 || units != 0) {
        std::snprintf(part, sizeof part, ",brid\\b_%03d", 0xE4 + units);
        out += part;
    }
    return out;
}

} // namespace

Bridge::Bridge(away::Session& session, Voyage& voyage) : session_(session), voyage_(voyage), space_(session.archive) {
    scene_.font = &session_.font;
    space_.setPlayer("ENTERPRI");
    space_.report = [this](int event, int what, bool player) { report(event, what, player); };
    space_.sound = [this](int n) { sound(n); };
    space_.music = [this](int song) { music(song); };
}

void Bridge::setZoom(int zoom) {
    zoom_ = std::clamp(zoom, 4, 8);
    if (fullView_) {
        space_.setView(ClipRect{2, 2, 0x140, 0xC8}, zoom_);
    } else {
        space_.setView(ClipRect{kViewLeft, kViewTop, kViewRight + 1, kViewBottom + 1}, zoom_);
    }
}

void Bridge::setFullView(bool on) {
    // The viewscreen alone shows space wider; the bridge's own a little
    // closer.
    fullView_ = on;
    setZoom(on ? 4 : 6);
    if (!on) {
        setControl(control_);
    }
}

void Bridge::report(int event, int what, bool player) {
    if (event == kRepaired) {
        post({kRepaired, what});
        return;
    }
    post({event, player ? 0 : 1});
}

void Bridge::music(int song) {
    song_ = song;
    if (song < 0) {
        session_.stopMusic();
        return;
    }
    session_.playMusic(song);
}

void Bridge::sound(int number) {
    if (const char* name = soundName(number)) {
        session_.playEffect(name);
    }
}

std::string Bridge::text(int index) { return session_.textLine(voyage_.textFile, index); }

int Bridge::sayAs(const std::string& name, const std::string& words, int x, int y, int colour, int number) {
    away::Ui u = ui();
    const int r = away::textBox(u, {name, words}, x, y, static_cast<std::uint8_t>(colour));
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
    if (number > 0) {
        post({kSaid, number});
    }
    return r;
}

int Bridge::sayAt(int officer, const std::string& words, int x, int y, int colour, int number) {
    return sayAs(kSpeakers[std::clamp(officer, 0, 7)].name, words, x, y, colour, number);
}

int Bridge::say(int officer, const std::string& words, int number) {
    const Speaker& s = kSpeakers[std::clamp(officer, 0, 7)];
    return sayAs(s.name, words, s.x, s.y, s.colour, number);
}

void Bridge::showComm(const std::string& picture) {
    static constexpr const char* kFoes[] = {"romula", "pira", "klg1", "klg2", "maddoc"};
    for (const char* f : kFoes) {
        if (picture == f) {
            music(0xF);
        }
    }
    if (picture == "mudd") {
        music(0x11);
    }
    if (fullView_) {
        setFullView(false);
    }
    comm_ = CrewMember{};
    comm_.frames = &session_.assets.anim(picture);
    comm_.present = !comm_.frames->empty();
    comm_.x = kViewLeft;
    comm_.y = kViewTop;
    comm_.nextTick = ticks_;
    if (comm_.present) {
        showFrame(comm_);
        comm_.sprite.depth = 1;
        comm_.nextTick = ticks_ + static_cast<std::uint64_t>(comm_.frames->front().ticks);
    }
}

void Bridge::hideComm() { comm_ = CrewMember{}; }

int Bridge::comm(const std::string& picture, const std::string& name, const std::string& words) {
    showComm(picture);
    const int r = sayAs(name, words, 0xA0, 0xBE, 0x2C);
    hideComm();
    return r;
}

int Bridge::choose(const std::vector<int>& lines) {
    std::vector<std::string> words;
    for (const int l : lines) {
        words.push_back(text(l));
    }
    return chooseWords(words);
}

int Bridge::chooseWords(const std::vector<std::string>& words) {
    std::vector<std::string> box{kSpeakers[kKirk].name};
    box.insert(box.end(), words.begin(), words.end());
    away::Ui u = ui();
    const int r = away::textBox(u, box, 0xA0, 0x82, 0xB0);
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
    return r;
}

std::string Bridge::enterCode() {
    away::Ui u = ui();
    const auto typed = away::textInput(u, "Code:", 0xA0, 0x64, 20);
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
    return typed.value_or(std::string{});
}

void Bridge::approach(int still) {
    // The ship drifts in from far ahead and comes to rest in front of the
    // Enterprise, slowing down as it nears; then the Enterprise holds
    // station beside it.
    static constexpr const char* kStills[8] = {"", "", "", "", "ark7", "derelict", "masada", "republic"};
    removeStill();
    constexpr int kScale = 2;
    const std::int32_t near = space_.focal() * kScale;
    const std::int32_t far = near * 6;
    const int i = space_.addStill(kStills[std::clamp(still, 4, 7)], space::Vec{0, 0, far});
    if (i < 0) {
        orbit(true, false);
        return;
    }
    space::Object& s = space_.object(i);
    const space::Object& p = space_.player();
    s.size = 0xFA;
    s.scale = kScale;
    s.axes = p.axes;
    voyage_.inOrbit = true;
    const bool wasFull = fullView_;
    setFullView(true);
    constexpr int kSteps = 100;
    for (int step = 1; step <= kSteps && !session_.platform.quit; ++step) {
        const std::int64_t left = kSteps - step;
        const auto z = static_cast<std::int32_t>(near + (far - near) * left * left / (kSteps * kSteps));
        s.pos = p.pos + space::applyTransposed(p.axes, space::Vec{0, 0, z});
        session_.waitTick();
        drawFrame();
    }
    setFullView(wasFull);
    s.pos = p.pos + space::applyTransposed(p.axes, space::Vec{0, 0, near});
    orbit(true, false);
}

void Bridge::removeStill() {
    for (int i = space::Space::kFirstStill; i < space::Space::kFirstStill + space::Space::kStills; ++i) {
        if (space_.object(i).kind == space::Kind::Still) {
            space_.remove(i);
        }
    }
}

void Bridge::resetShip() {
    space::Object& p = space_.player();
    p.shieldsUp = false;
    p.weaponsArmed = false;
    for (auto& part : p.cls.parts) {
        part.now = part.best;
    }
    for (auto& sh : p.cls.shields) {
        sh.now = sh.best;
    }
    space_.repairFirst = -1;
    setControl(control_);
}

void Bridge::clearShots() {
    for (int i = space::Space::kFirstShot; i < space::Space::kFirstShot + space::Space::kShots; ++i) {
        if (space_.object(i).kind == space::Kind::Shot) {
            space_.remove(i);
        }
    }
}

void Bridge::orbit(bool enter, bool announce) {
    if (announce) {
        sayAt(kSulu, enter ? "#BRID\\B_333#Entering standard orbit." : "#BRID\\B_335#Leaving orbit.", 0x7A, 0x74, 0xB0);
    }
    voyage_.inOrbit = enter;
    if (enter) {
        space::Object& p = space_.player();
        p.wantSpeed = 0;
        p.speed = 0;
        space_.steer(0, 0);
    }
}

// A report's text has the voices of two numbers and the numbers themselves
// to fill in: "%s" the voices, "%d" the numbers, in that order.
std::string Bridge::fillReport(const std::string& format, int first, int second) {
    std::string report;
    const std::string v1 = numberVoices(first);
    const std::string v2 = numberVoices(second);
    int strings = 0;
    int numbers = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        const char c = format[i];
        if (c == '%' && i + 1 < format.size()) {
            const char k = format[i + 1];
            if (k == 's') {
                report += strings++ == 0 ? v1 : v2;
                ++i;
                continue;
            }
            if (k == 'd') {
                report += std::to_string(numbers++ == 0 ? first : second);
                ++i;
                continue;
            }
            if (k == '%') {
                report += '%';
                ++i;
                continue;
            }
        }
        report += c;
    }
    return report;
}

int Bridge::yearReview(int textIndex) {
    int total = 0;
    for (int i = 0; i < 7; ++i) {
        total += std::max(voyage_.commendations[static_cast<std::size_t>(i)], 0);
    }
    // Four commendations for each of the seven scores is perfect.
    const int percent = std::clamp(total * 100 / 28, 0, 100);
    sayAs("Admiral", fillReport(text(textIndex), percent, 0), 0xA0, 0xBE, 0x2C);
    return percent;
}

void Bridge::score(int mission, int textIndex, int got, int best) {
    // The share of the best possible, and what it is worth.
    int percent = 0;
    if (best > 0) {
        percent = std::clamp(got, 0, best) * 100 / best;
    }
    int commendations = 0;
    int song = 0xD;
    const char* verdict = "#BRID\\B_199#I'll be frank, Kirk. Starfleet expects more of you than that. Try to do better on your next "
                          "assignment.";
    if (percent >= 100) {
        commendations = 4;
        song = 0xE;
        verdict = "#BRID\\B_195#A perfect mission, Jim! You are a model for all Starfleet!";
    } else if (percent >= 86) {
        commendations = 3;
        song = 0xC;
        verdict = "#BRID\\B_414#The top brass at Starfleet are impressed. Outstanding work, Jim.";
    } else if (percent >= 71) {
        commendations = 2;
        song = 0xB;
        verdict = "#BRID\\B_214#Well done, Captain. Keep up the good work.";
    } else if (percent >= 51) {
        commendations = 1;
        verdict = "#BRID\\B_197#A satisfactory performance, Captain, but there's still room for improvement.";
    }
    music(song);
    const std::string report = fillReport(text(textIndex), percent, commendations);
    showComm("woman");
    sayAs("Admiral", report, 0xA0, 0xBE, 0x2C);
    sayAs("Admiral", verdict, 0xA0, 0xBE, 0x2C);
    // Scores are numbered 0..6: the fifth and sixth missions share one,
    // so the last mission's is 6. After it the Admiral stays on screen.
    if (mission != 6) {
        hideComm();
    }
    voyage_.lastScored = mission;
    voyage_.commendations[static_cast<std::size_t>(std::clamp(mission, 0, kMissionCount - 1))] = commendations;
    voyage_.rating += commendations / 2;
    voyage_.points += commendations * 15;
    // Starfleet rewards a crew with a better ship.
    space::Object& p = space_.player();
    for (int i = 0; i < 4; ++i) {
        p.cls.parts[static_cast<std::size_t>(space::kPortTorpedoes + i)].best += commendations / 4;
    }
    for (auto& sh : p.cls.shields) {
        sh.best += commendations / 2;
    }
}

void Bridge::flyby(const std::string& title, const std::string& voice, int stage) {
    voyage_.flyby = true;
    voyage_.flybyTitle = title;
    voyage_.flybyVoice = voice;
    voyage_.flybyStage = stage;
    voyage_.busy = true;
    music(0xB);
    session_.playEffect("BRIDLOOP", true);
    setFullView(true);
    space_.resetStars();
    space::Object& p = space_.player();
    p.axes = space::Mat::identity();
    space::yaw(p.axes, 0x100);
    p.wantSpeed = 0;
    p.speed = 0;
    p.velocity = {};
    space_.watchFrom({1974, 0, 720});
}

void Bridge::flybyEvent(const Event& e) {
    if (e.type == kTick) {
        // The ship flies along x, the watcher a little slower beside it.
        const std::int32_t t = static_cast<std::int32_t>(voyage_.clock);
        space::Object& p = space_.player();
        if (voyage_.clock <= 0x69) {
            p.pos = {125 * t, 0, 0};
            space_.watchFrom({100 * t + 1974, 0, 720});
        }
        switch (voyage_.clock) {
        case 2:
            space_.resetStars();
            break;
        case 0x41:
            if (!voyage_.flybyVoice.empty()) {
                session_.playSpeech(voyage_.flybyVoice);
            }
            break;
        case 0x5F:
            try {
                auto b = std::make_shared<Bitmap>(Bitmap::fromBytes(session_.archive.need(away::upper(voyage_.flybyTitle) + ".BMP")));
                b->handleX = b->width() / 2;
                b->handleY = b->height() / 2;
                title_.bitmap = b;
                title_.x = 0xA0;
                title_.y = 0x3C;
                title_.depth = 0;
                showTitle_ = true;
            } catch (const FormatError&) {
            }
            break;
        case 0x69: {
            showTitle_ = false;
            space_.watchFromBridge();
            setFullView(false);
            p.wantSpeed = p.cls.maxSpeed / 2;
            break;
        }
        case 0x96:
            voyage_.flyby = false;
            voyage_.stage = voyage_.flybyStage;
            voyage_.busy = false;
            setControl(true);
            break;
        default:
            break;
        }
    }
    defaultEvent(e);
}

void Bridge::panelPicture(const std::string& name, bool opaque, int recolour) {
    // The bridge's panel pictures carry their place on the screen as a
    // negative handle.
    std::shared_ptr<const Bitmap> b;
    try {
        b = session_.assets.picture(name);
    } catch (const FormatError&) {
        return;
    }
    const int left = -b->handleX;
    const int top = -b->handleY;
    for (int y = 0; y < b->height(); ++y) {
        for (int x = 0; x < b->width(); ++x) {
            std::uint8_t c = b->pixels.at(x, y);
            if (c == 0 && !opaque) {
                continue;
            }
            if (c != 0 && recolour >= 0) {
                c = static_cast<std::uint8_t>(recolour);
            }
            scene_.backdrop.set(left + x, top + y, c);
        }
    }
}

void Bridge::drawShipPanels(const space::Object& ship) {
    static constexpr const char* kNames[4] = {"ent", "klg", "ori", "rom"};
    const std::string name = kNames[std::clamp(ship.type, 0, 3)];
    panelPicture("t" + name, true);
    panelPicture("s" + name, true);
    // Parts below three quarters of their strength light up.
    for (int i = 0; i < 10; ++i) {
        const space::Part& part = ship.cls.parts[static_cast<std::size_t>(i)];
        if (part.now * 4 < part.best * 3) {
            char n[16];
            std::snprintf(n, sizeof n, "%02d", i);
            panelPicture("t" + name + n, false);
            panelPicture("s" + name + n, false);
        }
    }
    // The shields, from bright (strong) to dark (weak or down).
    for (int i = 0; i < 6; ++i) {
        const space::Part& sh = ship.cls.shields[static_cast<std::size_t>(i)];
        const int colour = ship.shieldsWork ? 0xB0 + (sh.best - sh.now) * 8 / (sh.best + 1) : 0xB7;
        char n[24];
        std::snprintf(n, sizeof n, "%sshd%02d", name.c_str(), i);
        panelPicture(n, false, colour);
    }
}

void Bridge::drawRadar() {
    if (!radarBitmap_) {
        radarBitmap_ = std::make_shared<Bitmap>();
        radarBitmap_->pixels = Surface(17, 17);
        radarBitmap_->handleX = 8;
        radarBitmap_->handleY = 8;
        radar_.x = 0x9F;
        radar_.y = 0x8C;
        radar_.depth = 2;
    }
    Surface& r = radarBitmap_->pixels;
    r.fill(0);
    r.set(8, 8, 0x8B);
    static constexpr std::uint8_t kColours[4] = {46, 120, 100, 86};
    for (int i = 0; i < space::Space::kShips; ++i) {
        const space::Object& o = space_.object(space::Space::kFirstShip + i);
        if (o.kind != space::Kind::Ship || (o.cloaked && o.cloakLevel >= 20)) {
            continue;
        }
        const space::Vec v = space::apply(space_.player().axes, o.pos - space_.player().pos);
        const double across = std::hypot(static_cast<double>(v.x), static_cast<double>(v.y));
        // How far off the bow (0 ahead, 180 degrees behind) sets the
        // distance from the middle; the direction is where it lies.
        const double off = std::numbers::pi / 2 - std::atan2(static_cast<double>(v.z), across);
        const double radius = 16.0 * std::sin(off / 2);
        const double dir = std::atan2(static_cast<double>(v.y), static_cast<double>(v.x));
        const int x = static_cast<int>((radius * std::cos(dir) + 1) / 2) + 8;
        const int y = static_cast<int>((radius * -std::sin(dir) + 1) / 2) + 8;
        r.set(x, y, kColours[i]);
    }
    radar_.bitmap = radarBitmap_;
}

void Bridge::drawHud() {
    Surface& to = scene_.backdrop;
    const space::Object& p = space_.player();
    auto line = [&to](int x, int y, int len, std::uint8_t c) {
        for (int i = 0; i < len; ++i) {
            to.set(x + i, y, c);
        }
    };
    // The weapons: a bar fills as each comes ready again, with a light.
    if (!weaponLights_) {
        try {
            weaponLights_ = session_.assets.picture("phasind");
        } catch (const FormatError&) {
        }
    }
    static constexpr int kWeaponAt[4][2] = {{196, 16}, {194, 20}, {90, 16}, {92, 20}};
    for (int i = 0; i < 4; ++i) {
        const space::Part& part = p.cls.parts[static_cast<std::size_t>(space::kPortTorpedoes + i)];
        const space::Mount& m = p.cls.mounts[static_cast<std::size_t>(i)];
        int fill = 24;
        int light = 0;
        std::uint8_t colour = 0x81;
        if (!p.weaponsArmed) {
            light = 0;
        } else if (!p.weaponsWork || part.now < part.best / 2) {
            light = 0x18;
        } else {
            colour = static_cast<std::uint8_t>(0xB1 + (i & 1));
            if (m.readyAt > space_.ticks()) {
                fill = 24 - static_cast<int>((m.readyAt - space_.ticks()) * 24 / static_cast<std::uint64_t>(std::max(1, m.reload)));
                light = 8;
            } else {
                light = 0x10;
            }
        }
        const int x = kWeaponAt[i][0];
        const int y = kWeaponAt[i][1];
        line(x, y, fill, colour);
        line(x + fill, y, 24 - fill, 0x81);
        if (weaponLights_) {
            const int row = x > 0xA0 ? 3 : 0;
            for (int yy = 0; yy < 3; ++yy) {
                for (int xx = 0; xx < 8; ++xx) {
                    if (weaponLights_->pixels.contains(light + xx, row + yy)) {
                        to.set(x + 0x1B + xx, y - 1 + yy, weaponLights_->pixels.at(light + xx, row + yy));
                    }
                }
            }
        }
    }
    // The warp engines' power.
    static constexpr int kPowerAt[2][2] = {{140, 13}, {140, 16}};
    for (int i = 0; i < 2; ++i) {
        const space::Part& part = p.cls.parts[static_cast<std::size_t>(space::kPortWarp + i)];
        const int value = std::min(part.best, part.now + (p.powerBoost > 0 ? 5 : 0));
        const int len = part.best > 0 ? 0x22 * value / part.best : 0;
        const int x = kPowerAt[i][0];
        const int y = kPowerAt[i][1];
        for (int row = 0; row < 2; ++row) {
            line(x, y + row, len, 0xB1);
            line(x + len, y + row, 0x22 - len, 0x81);
            line(x + 0x23, y + row, 4, p.powerBoost > 0 ? 0x69 : 0x81);
        }
    }
    // Speed: asked for, and made.
    const std::int32_t maxSpeed = std::max<std::int32_t>(1, p.cls.maxSpeed);
    const int want = std::max(0, static_cast<int>(static_cast<std::int64_t>(p.wantSpeed) * 25 / maxSpeed));
    line(0x8F, 0x14, want, 0x69);
    line(0x8F + want, 0x14, 25 - want, 0);
    const int made = std::max(0, static_cast<int>(static_cast<std::int64_t>(p.speed) * 24 / maxSpeed));
    line(0x90, 0x16, made, 0xBA);
    line(0x90 + made, 0x16, 24 - made, 0);
    if ((space_.ticks() & 3) == 0) {
        const space::Object* shown = &p;
        if (targetAnalysis_ && space_.lastHit >= 0 && space_.object(space_.lastHit).kind == space::Kind::Ship) {
            shown = &space_.object(space_.lastHit);
        }
        drawShipPanels(*shown);
    }
    drawRadar();
}

void Bridge::steer() {
    if (voyage_.inOrbit || voyage_.flyby) {
        return;
    }
    const Platform& p = session_.platform;
    if (control_ && !voyage_.busy) {
        space_.steer(p.mouseX - space_.centreX(), p.mouseY - space_.centreY());
    }
    space_.setRoll(p.heldKey == 0x52 ? 1 : (p.heldKey == 0x53 ? -1 : 0));
}

const char* Bridge::speakerName(int officer) { return kSpeakers[std::clamp(officer, 0, 7)].name; }

void Bridge::load() {
    auto& archive = session_.archive;
    session_.platform.palette = Palette::fromBytes(archive.need("BRIDGE.PAL"));
    if (const auto lut = archive.load("BRIDGE.LUT"); lut && lut->size() >= 256) {
        std::copy_n(lut->begin(), 256, scene_.darken.begin());
    }
    scene_.backdrop.fill(0);
    Bitmap::fromBytes(archive.need("BRIDGE.BMP")).copyTo(scene_.backdrop, 0, 0);
    if (const auto pri = archive.load("BRIDGE.PRI")) {
        scene_.setDepths(*pri);
    }
    if (!crew_[kKirk].present) {
        placeCrew(kKirk, "bstndki", 0, 4);
        placeCrew(kSpock, "bstndsp", 0, 0);
        placeCrew(kUhura, "bstnduh", 0, 0);
        placeCrew(kChekov, "bstndch", 0, 0);
        placeCrew(kSulu, "bstndsu", 0, 0);
        placeCrew(kScott, "bstndsc", 0, 0);
    }

    // A light that runs along the console under the viewscreen.
    Bitmap light;
    light.pixels = Surface(8, 3);
    light.pixels.fill(0xDA);
    lightBitmap_ = std::make_shared<const Bitmap>(std::move(light));
    light_.bitmap = lightBitmap_;
    light_.x = 0x49;
    light_.y = 0x6B;
    light_.depth = 2;
}

void Bridge::placeCrew(int officer, const std::string& anim, int x, int y) {
    CrewMember& c = crew_[static_cast<std::size_t>(std::clamp(officer, 0, 7))];
    std::string name = anim;
    if (session_.archive.has(away::upper(name) + "J.ANM")) {
        name += "j";
    }
    c.frames = &session_.assets.anim(name);
    c.present = !c.frames->empty();
    c.frame = 0;
    c.x = x;
    c.y = y;
    c.nextTick = ticks_;
    if (c.present) {
        showFrame(c);
        c.nextTick = ticks_ + static_cast<std::uint64_t>(c.frames->front().ticks);
    }
}

void Bridge::removeCrew(int officer) { crew_[static_cast<std::size_t>(std::clamp(officer, 0, 7))] = CrewMember{}; }

void Bridge::showFrame(CrewMember& c) {
    const away::AnimFrame& f = (*c.frames)[static_cast<std::size_t>(c.frame)];
    try {
        c.sprite.bitmap = session_.assets.picture(f.picture);
    } catch (const FormatError&) {
        c.sprite.bitmap.reset();
    }
    c.sprite.x = c.x + f.x;
    c.sprite.y = c.y + f.y;
    c.sprite.depth = scene_.depthAt(0, std::clamp(c.y + f.y, 0, 199)) + f.depth;
    c.sprite.mode = away::SpriteMode::Normal;
}

void Bridge::animate(CrewMember& c) {
    if (!c.present || c.frames == nullptr || c.frames->empty() || ticks_ < c.nextTick) {
        return;
    }
    const int count = static_cast<int>(c.frames->size());
    const int next = (*c.frames)[static_cast<std::size_t>(std::min(c.frame, count - 1))].next[randomInt(4)];
    c.frame = next < count ? next : 0;
    showFrame(c);
    c.nextTick = ticks_ + static_cast<std::uint64_t>(std::max(1, (*c.frames)[static_cast<std::size_t>(c.frame)].ticks));
}

void Bridge::updateCrew() {
    for (auto& c : crew_) {
        animate(c);
    }
    const int depth = comm_.sprite.depth;
    animate(comm_);
    comm_.sprite.depth = depth;
}

void Bridge::drawFrame() {
    Surface& screen = session_.platform.screen;
    if (fullView_) {
        space_.draw(screen);
        if (showTitle_ && title_.bitmap) {
            scene_.drawSprite(screen, title_);
        }
        return;
    }
    space_.draw(scene_.backdrop);
    drawHud();
    std::vector<const away::Sprite*> sprites;
    if (radar_.bitmap) {
        sprites.push_back(&radar_);
    }
    if (comm_.present && comm_.sprite.bitmap) {
        sprites.push_back(&comm_.sprite);
    }
    for (const auto& c : crew_) {
        if (c.present && c.sprite.bitmap) {
            sprites.push_back(&c.sprite);
        }
    }
    sprites.push_back(&light_);
    scene_.draw(screen, sprites);
}

void Bridge::simulate() {
    ++ticks_;
    steer();
    if (!voyage_.inOrbit) {
        space_.tick();
    }
    voyage_.underAttack = space_.enemiesLeft();
    updateCrew();
    if ((ticks_ & 3) == 0) {
        lightStep_ = (lightStep_ + 1) % 22;
        light_.x = 0x49 + lightStep_ * 8;
    }
}

void Bridge::advance() {
    post({kTick, static_cast<int>(voyage_.clock)});
    ++voyage_.clock;
    while (!events_.empty() && !result_) {
        const Event e = events_.front();
        events_.pop_front();
        dispatch(e);
    }
    drawFrame();
}

away::Ui Bridge::ui() {
    return away::Ui{session_, scene_, [this] { drawFrame(); }};
}

void Bridge::setControl(bool on) {
    control_ = on;
    Platform& p = session_.platform;
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
    p.warpPointer = true;
    p.warpX = 0xA0;
    p.warpY = control_ ? (kViewTop + kViewBottom) / 2 : 0xA6;
}

void Bridge::clampPointer() {
    Platform& p = session_.platform;
    int x = p.mouseX;
    int y = p.mouseY;
    if (control_) {
        const ClipRect& v = space_.view();
        x = std::clamp(x, v.left, v.right - 1);
        y = std::clamp(y, v.top, v.bottom - 1);
    } else {
        y = std::max(y, kViewBottom + 4);
    }
    if (x != p.mouseX || y != p.mouseY) {
        p.warpPointer = true;
        p.warpX = x;
        p.warpY = y;
    }
}

void Bridge::leftClick() {
    Platform& p = session_.platform;
    if (control_) {
        post({kOrder, kFirePhasers});
        return;
    }
    std::vector<const away::Sprite*> sprites;
    for (const auto& c : crew_) {
        if (c.present && c.sprite.bitmap) {
            sprites.push_back(&c.sprite);
        }
    }
    away::sortForDrawing(sprites);
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        if (!(*it)->hit(p.mouseX, p.mouseY)) {
            continue;
        }
        for (int i = 0; i < 8; ++i) {
            if (&crew_[static_cast<std::size_t>(i)].sprite != *it || kMenus[i].menu[0] == 0) {
                continue;
            }
            session_.playEffect("BUTTON1");
            away::Ui u = ui();
            const int choice = away::buttonMenu(u, kMenus[i].menu, kMenus[i].x, kMenus[i].y, 0);
            away::setPointer(session_, "pushbtn");
            if (choice >= 0) {
                post({kOrder, choice});
            }
            return;
        }
        return;
    }
}

void Bridge::handleInput() {
    Platform& p = session_.platform;
    clampPointer();
    const std::uint8_t clicks = p.takeClicks();
    if (voyage_.busy) {
        // A scene runs: the player only watches (and may skip speech).
        p.keys.clear();
        return;
    }
    if ((clicks & 1) != 0) {
        leftClick();
    } else if ((clicks & 2) != 0 && control_) {
        post({kOrder, kFireTorpedoes});
    }
    while (!p.keys.empty()) {
        const KeyPress key = p.keys.front();
        p.keys.pop_front();
        switch (key.scancode) {
        case scancode::kTab:
        case 0x3D: // F3
            setControl(!control_);
            break;
        case scancode::kEnter:
        case scancode::kF1:
            if (control_) {
                post({kOrder, kFirePhasers});
            } else {
                leftClick();
            }
            break;
        case scancode::kSpace:
        case 0x3C: // F2
            if (control_) {
                post({kOrder, kFireTorpedoes});
            }
            break;
        case 0x11: post({kOrder, kWeapons}); break;        // W
        case 0x1F: post({kOrder, kShields}); break;        // S
        case 0x12: post({kOrder, kRepairMenu}); break;     // E
        case 0x20: post({kOrder, kDamageReport}); break;   // D
        case 0x1E: post({kOrder, kTargetAnalysis}); break; // A
        case 0x23: post({kOrder, kHail}); break;           // H
        case 0x14: post({kOrder, kSpockTalk}); break;      // T
        case 0x25: post({kOrder, kKirkMenu}); break;       // K
        case 0x18: post({kOrder, kOrbit}); break;          // O
        case 0x2F: post({kOrder, kToggleViewscreen}); break; // V
        case 0x31: post({kOrder, kStarMap}); break;        // N
        case 0x2E: post({kOrder, kComputer}); break;       // C
        case 0x2D: post({kOrder, kTransporter}); break;    // X
        case 0x26: post({kOrder, kCaptainsLog}); break;    // L
        case scancode::kEscape:
            post({kOrder, kOptions});
            break;
        case 0x29: // `: full speed backwards
            if (!voyage_.inOrbit) space_.fullReverse();
            break;
        case 0x4C: // keypad 5: stop turning
            space_.steer(0, 0);
            space_.setRoll(0);
            if (control_) {
                p.warpPointer = true;
                p.warpX = space_.centreX();
                p.warpY = space_.centreY();
            }
            break;
        case 0x33: // , and .: zoom out and in
            setZoom(zoom_ - 1);
            say(kSulu, "Magnification " + std::to_string(zoom_ - 3));
            break;
        case 0x34:
            setZoom(zoom_ + 1);
            say(kSulu, "Magnification " + std::to_string(zoom_ - 3));
            break;
        default:
            if (key.scancode >= 0x02 && key.scancode <= 0x0B && !voyage_.inOrbit) {
                space_.setSpeedLevel(key.scancode - 0x02); // 1 stops, 0 is full speed
            }
            break;
        }
    }
}

void Bridge::dispatch(const Event& e) {
    if (voyage_.flyby) {
        flybyEvent(e);
        return;
    }
    if (voyage_.encounter != 0) {
        encounterEvent(*this, e);
        return;
    }
    missionEvent(*this, e);
}

void Bridge::defaultEvent(const Event& e) {
    switch (e.type) {
    case kTick:
        simulate();
        break;
    case kOrder:
        defaultAction(e.what);
        break;
    case kShieldsFalling:
        if (e.what == 0) {
            sayAt(kScott, "#BRID\\BRID_S45#The shields are falling!", 0, 0xA0, 0xA1);
        }
        break;
    case kWeaponsOffLine:
        if (e.what == 0) {
            sayAt(kScott, "#BRID\\BRID_S37#Our weapons are off-line!", 0, 0xA0, 0xA1);
        }
        break;
    case kSpeedFalling:
        if (e.what == 0) {
            sayAt(kScott, "#BRID\\BRID_S47#We can't maintain our speed!", 0, 0xA0, 0xA1);
        }
        break;
    case kRepaired:
        for (const auto& r : kRepairedNames) {
            if (r.system == e.what || (e.what == 0x7FFF && r.system == 0xFF)) {
                sayAt(kScott, std::string(r.name) + " repaired, Captain.", 0x40, 0x64, 0xA1);
            }
        }
        break;
    default:
        break;
    }
}

void Bridge::defaultAction(int action) {
    space::Object& player = space_.player();
    switch (action) {
    case kKirkMenu: {
        away::Ui u = ui();
        const int choice = away::buttonMenu(u, "command", 60, 100, 0);
        away::setPointer(session_, control_ ? "entcur" : "pushbtn");
        if (choice >= 0) {
            post({kOrder, choice});
        }
        break;
    }
    case kOptions: {
        std::uint32_t hidden = 0;
        hidden |= 1u << ((session_.musicOn() ? 2 : 3) + 1);
        hidden |= 1u << ((session_.effectsOn ? 4 : 5) + 1);
        away::Ui u = ui();
        const int choice = away::buttonMenu(u, "options", 0x3C, 0x41, hidden);
        away::setPointer(session_, "pushbtn");
        switch (choice) {
        case 0: saveGame(); break;
        case 1: {
            away::Ui q = ui();
            const int number = away::loadGameMenu(q);
            if (number >= 0) {
                result_ = BridgeResult{Leave::LoadSave, number};
            }
            break;
        }
        case 2: session_.setMusicOn(true); break;
        case 3: session_.setMusicOn(false); break;
        case 4: session_.effectsOn = true; break;
        case 5: session_.effectsOn = false; session_.stopEffects(); break;
        case 6: {
            away::Ui q = ui();
            if (away::textBox(q, {"Quit Game", "#GENE\\GENER028#Yes, quit the game.", "#GENE\\GENER008#No, do not quit the game."},
                              0x14, 0x14, 0xB0) == 0) {
                result_ = BridgeResult{Leave::Quit, -1};
            }
            break;
        }
        default: break;
        }
        break;
    }
    case kCaptainsLog:
        captainsLog();
        break;
    case kComputer:
        computer();
        break;
    case kSpockTalk:
        sayAt(kSpock, "#BRID\\B_155#Nothing to report, Captain.", 0x126, 0x6A, 0x2C);
        break;
    case kHail:
        sayAt(kUhura, "#BRID\\BRIDU016#Hailing frequencies open.", 0x12A, 0x96, 0xA1);
        break;
    case kFirePhasers:
        if (!voyage_.inOrbit) {
            const Platform& p = session_.platform;
            space_.firePhasers(control_ ? p.mouseX : space_.centreX(), control_ ? p.mouseY : space_.centreY());
        }
        break;
    case kFireTorpedoes:
        if (!voyage_.inOrbit) {
            const Platform& p = session_.platform;
            space_.fireTorpedoes(control_ ? p.mouseX : space_.centreX(), control_ ? p.mouseY : space_.centreY());
        }
        break;
    case kToggleViewscreen:
        setFullView(!fullView_);
        if (!fullView_) {
            setControl(true);
        }
        break;
    case kShields:
        player.shieldsUp = !player.shieldsUp;
        sayAt(kSulu, player.shieldsUp ? "#BRID\\B_340#Raising shields." : "#BRID\\B_337#Lowering shields, Captain.", 0x7A, 0x74, 0xB0);
        break;
    case kWeapons:
        player.weaponsArmed = !player.weaponsArmed;
        sayAt(kChekov, player.weaponsArmed ? "#BRID\\B_351#Arming weapons." : "#BRID\\B_354#Disarming weapons.", 0xC4, 0x74, 0xB0);
        setControl(control_);
        break;
    case kRepairFirst + 0: space_.repairFirst = 0x7FFF; break;
    case kRepairFirst + 1: space_.repairFirst = space::kPortPhasers; break;
    case kRepairFirst + 2: space_.repairFirst = space::kPortTorpedoes; break;
    case kRepairFirst + 3: space_.repairFirst = space::kSensors; break;
    case kRepairFirst + 4: space_.repairFirst = space::kBridge; break;
    case kRepairFirst + 5: space_.repairFirst = space::kHull; break;
    case kRepairFirst + 6: space_.repairFirst = space::kImpulse; break;
    case kTargetAnalysis:
        targetAnalysis_ = !targetAnalysis_;
        sayAt(kSulu, targetAnalysis_ ? "#BRID\\B_344#Target Analysis On." : "#BRID\\B_343#Target Analysis Off.", 0x7A, 0x74, 0xB0);
        break;
    case kTransporter:
        if (player.shieldsUp) {
            sayAt(kSulu, "#BRID\\B_332#Captain, the shields are up.", 0x7A, 0x74, 0xA1);
        } else if (!voyage_.inOrbit) {
            sayAt(kSulu, "#BRID\\B_350#We're not in orbit, Captain.", 0x7A, 0x74, 0xA1);
        } else if (voyage_.system == voyage_.destination) {
            if (voyage_.mission == kFeather) {
                sayAt(kKirk, "#BRID\\C_006#Assemble a landing party. Unless we find this so-called criminal, we're going to war.", 0xA0, 0x82,
                      0xB0);
            } else if (voyage_.mission != kVeng) {
                sayAt(kKirk, "#BRID\\C_060#Spock, come with me. Mr Scott, you have the conn.", 0xA0, 0x82, 0xB0);
            }
            beamDown();
        } else {
            sayAt(kSulu, "#BRID\\B_346#This isn't our destination.", 0x7A, 0x74, 0xB0);
        }
        break;
    case kOrbit:
        if (voyage_.underAttack) {
            sayAt(kSulu, "#BRID\\B_348#Unable to comply, Captain. We're under attack.", 0x7A, 0x74, 0xB0);
        } else if (voyage_.system == voyage_.destination && !voyage_.inOrbit) {
            orbit(true, true);
        } else {
            sayAt(kSulu, "#BRID\\B_338#May I respectfully remind the Captain that we haven't accomplished our mission, Sir.", 0x7A, 0x74,
                  0x2C);
        }
        break;
    case kStarMap:
        if (voyage_.underAttack) {
            sayAt(kSulu, "#BRID\\B_348#Unable to comply, Captain.  We're under attack.", 0x7A, 0x74, 0xB0);
        } else if (voyage_.system == voyage_.destination) {
            sayAt(kSulu, "#BRID\\B_338#May I respectfully remind the Captain that we haven't accomplished our mission, Sir.", 0x7A, 0x74,
                  0x2C);
        } else if (voyage_.inOrbit) {
            sayAt(kSulu, "#BRID\\B_349#We must first leave orbit, Sir.", 0x7A, 0x74, 0xB0);
        } else {
            starMap();
            space_.resetStars();
        }
        break;
    case kRepairMenu: {
        away::Ui u = ui();
        const int choice = away::buttonMenu(u, "repair", 0x1E, 0x1E, 0);
        away::setPointer(session_, "pushbtn");
        if (choice >= 0) {
            post({kOrder, choice});
        }
        break;
    }
    case kDamageReport: {
        // Scott judges by the hull.
        const space::Part& hull = player.cls.parts[space::kHull];
        if (hull.now * 2 > hull.best) {
            sayAt(kScott, "#BRID\\BRID_S32#I don't know how long she can take it, Captain.", 0x40, 0x64, 0xA1);
        } else {
            sayAt(kScott, "#BRID\\BRID_S41#She can't take it, Captain.", 0x40, 0x64, 0xA1);
        }
        break;
    }
    default:
        break;
    }
}

void Bridge::starMap() {
    // The map fills the screen; the system nearest the pointer is circled
    // and a click (or Enter) sets course for it.
    Platform& p = session_.platform;
    const Palette saved = p.palette;
    Surface map(Surface::kScreenWidth, Surface::kScreenHeight);
    map.fill(0);
    try {
        Bitmap::fromBytes(session_.archive.need("STARMAP.BMP")).copyTo(map, 0, 0);
    } catch (const FormatError&) {
    }
    std::shared_ptr<const Bitmap> circle;
    try {
        circle = session_.assets.picture("circle");
    } catch (const FormatError&) {
    }
    away::setPointer(session_, "cross");
    p.takeClicks();
    p.keys.clear();
    int chosen = 0;
    while (!p.quit) {
        int best = 0x7FFF;
        for (int i = 0; i < 45; ++i) {
            const int d = std::abs(kSystems[i][0] - p.mouseX) + std::abs(kSystems[i][1] - p.mouseY);
            if (d < best) {
                best = d;
                chosen = i;
            }
        }
        p.screen.blit(map, 0, 0, Surface::kScreenWidth, Surface::kScreenHeight, 0, 0);
        if (circle) {
            away::Sprite s;
            s.bitmap = circle;
            s.x = kSystems[chosen][0];
            s.y = kSystems[chosen][1];
            s.mode = away::SpriteMode::Overlay;
            away::Scene plain;
            plain.drawSprite(p.screen, s);
        }
        session_.waitTick();
        bool done = (p.takeClicks() & 1) != 0;
        while (!p.keys.empty()) {
            const std::uint8_t k = p.keys.front().scancode;
            p.keys.pop_front();
            if (k == scancode::kEnter || k == scancode::kSpace || k == scancode::kF1 || k == 0x3C) {
                done = true;
            }
        }
        if (done) {
            break;
        }
    }
    p.palette = saved;
    setControl(control_);
    // Warp to it.
    sound(0x29);
    voyage_.system = chosen;
    voyage_.inOrbit = false;
    if (voyage_.system != voyage_.destination) {
        startEncounter(*this);
    }
}

void Bridge::captainsLog() {
    static const char* const kTitles[] = {"Demon World",       "Hijacked",           "Love's Labor Jeopardized", "Another Fine Mess",
                                          "Feathered Serpent", "That Old Devil Moon", "Vengeance"};
    const int last = std::clamp(voyage_.lastScored, -1, 6);
    if (last < 0) {
        sayAs("Captain's Log", "#BRID\\C_007#No recent missions have been completed.", 0xA0, 0x82, 0xB0);
        return;
    }
    auto points = [](int n) { return n == 1 ? std::string("1 point") : std::to_string(n) + " points"; };
    int total = 0;
    for (int i = last; i >= 0; --i) {
        total += std::max(voyage_.commendations[static_cast<std::size_t>(i)], 0);
    }
    std::string log = "#BRID\\C_007#We have earned " + points(total) + " for the following recently completed missions:\n\n";
    for (int i = last; i >= 0; --i) {
        log += std::string(kTitles[i]) + ":  " + points(std::max(voyage_.commendations[static_cast<std::size_t>(i)], 0)) + ".\n\n";
    }
    sayAs("Captain's Log", log, 0xA0, 0x82, 0xB0);
}

namespace {

std::string upperText(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

// The library computer's entries: DB.KEY has a line for each, its file
// ("file004") and then the words it is found by ("ARK7 ARK-7 Ark-7").
struct ComputerEntry {
    std::string file;
    std::string words; // upper case, with a space before each word
};

std::vector<ComputerEntry> computerIndex(const Archive& archive) {
    std::vector<ComputerEntry> entries;
    const auto bytes = archive.load("DB.KEY");
    if (!bytes) {
        return entries;
    }
    std::string line;
    auto finish = [&] {
        const auto space = line.find(' ');
        if (space != std::string::npos && space > 0 && space <= 8) {
            entries.push_back({line.substr(0, space), upperText(line.substr(space))});
        }
        line.clear();
    };
    for (const std::uint8_t b : *bytes) {
        if (b == '$' && line.empty()) {
            break;
        }
        if (b == '\n') {
            finish();
        } else if (b != '\r' && b != 0x1A) {
            line += static_cast<char>(b);
        }
    }
    finish();
    return entries;
}

// An entry's file: its title, then its voice, then the text. A single
// line break only wraps; an empty line starts a new paragraph.
std::string computerText(const Archive& archive, const std::string& file) {
    const auto bytes = archive.load(upperText(file) + ".DB");
    if (!bytes) {
        return {};
    }
    std::vector<std::string> lines;
    std::string line;
    for (const std::uint8_t b : *bytes) {
        if (b == '$' || b == 0) {
            break;
        }
        if (b == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (b != '\r') {
            line += static_cast<char>(b);
        }
    }
    lines.push_back(line);
    if (lines.empty()) {
        return {};
    }
    std::string voice;
    std::size_t first = 1;
    if (lines.size() > 1 && !lines[1].empty() && lines[1][0] == '#') {
        voice = lines[1];
        first = 2;
    }
    std::string text = voice + lines[0] + "\n\n";
    bool fresh = true;
    for (std::size_t i = first; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            if (!fresh) {
                text += "\n\n";
                fresh = true;
            }
            continue;
        }
        if (!fresh) {
            text += ' ';
        }
        text += lines[i];
        fresh = false;
    }
    return text;
}

} // namespace

void Bridge::computer() {
    const auto index = computerIndex(session_.archive);
    away::Ui u = ui();
    while (!session_.platform.quit) {
        const auto typed = away::textInput(u, "Computer:", 0xA0, 0x64, 20);
        if (!typed || typed->empty()) {
            break;
        }
        std::vector<std::string> found = {"Computer"};
        if (typed->size() > 2) {
            const std::string wanted = " " + upperText(*typed);
            for (const ComputerEntry& e : index) {
                if (e.words.find(wanted) != std::string::npos && found.size() <= 16) {
                    std::string text = computerText(session_.archive, e.file);
                    if (!text.empty()) {
                        found.push_back(std::move(text));
                    }
                }
            }
        }
        if (found.size() == 1) {
            sayAs("Spock", "#COMP\\COMPUNOT#Topic not available, Captain.", 0x126, 0x6A, 0x2C);
            continue;
        }
        away::textBox(u, found, 0x14, 0x14, 0xB0);
    }
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
}

void Bridge::saveState(SaveWriter& w) const { space_.save(w); }

void Bridge::restoreState(SaveReader& r) {
    space_.restore(r);
    space_.watchFromBridge();
    clearEvents();
}

void Bridge::saveGame() {
    SaveWriter w;
    w.text("bridge");
    session_.save(w);
    w.u8(1);
    voyage_.save(w);
    saveState(w);
    away::Ui u = ui();
    away::saveGameMenu(u, missionName(voyage_.mission), w.data);
}

void Bridge::shipLost() {
    // Seen from outside as it breaks apart, then the end.
    Platform& p = session_.platform;
    setFullView(true);
    const space::Object& ship = space_.player();
    space_.watchFrom(ship.pos + space::applyTransposed(ship.axes, space::Vec{0, 200, -1500}));
    p.pointerShown = false;
    for (int i = 0; i < 60 && !p.quit; ++i) {
        session_.waitTick();
        space_.tick();
        drawFrame();
    }
    space_.watchFromBridge();
    p.pointerShown = true;
    setFullView(false);
    away::Ui u = ui();
    away::textBox(u, {"", "#GENE\\SPACE000#The Enterprise has been destroyed!"}, 0x14, 0x14, 0xB0);
    gameOver();
}

void Bridge::gameOver() {
    away::Ui u = ui();
    int load = -1;
    switch (away::gameOverMenu(u, load)) {
    case away::GameOverChoice::Load: result_ = BridgeResult{Leave::LoadSave, load}; break;
    case away::GameOverChoice::Restart: result_ = BridgeResult{Leave::Restart, -1}; break;
    case away::GameOverChoice::Quit: result_ = BridgeResult{Leave::Quit, -1}; break;
    }
}

// Moves the camera and a credit card on for some frames of the closing
// titles; gives false when the player skips them.
bool Bridge::creditFrames(int index, std::int32_t speed, int frames) {
    Platform& p = session_.platform;
    for (int i = 0; i < frames; ++i) {
        if (p.quit || p.takeSkip()) {
            return false;
        }
        camera_.z += 10;
        if (index >= 0) {
            space_.object(index).pos.z += speed + 10;
        }
        space_.watchAlongZ(camera_);
        drawFrame();
        for (int t = 0; t < 3; ++t) {
            session_.waitTick();
        }
    }
    return !p.quit;
}

void Bridge::endGame() {
    // The names of the people who made the game, out in space with the
    // theme playing, and then a last word for the show's creator.
    Platform& p = session_.platform;
    session_.stopEffects();
    session_.loadMusic("title");
    session_.playMusic(0);
    setFullView(true);
    p.screen.fill(0);
    p.pointerShown = false;
    p.takeSkip();
    camera_ = space_.player().pos;
    const std::int32_t focal = space_.focal();
    auto card = [&](const char* name) {
        const int index = space_.addStill(name, camera_);
        if (index >= 0) {
            space_.object(index).scale = 1;
        }
        return index;
    };
    auto drop = [&](int& index) {
        if (index >= 0) {
            space_.remove(index);
            index = -1;
        }
    };
    bool going = true;
    int index = card("credit00");
    // It comes out from the watcher and stops at its own size.
    going = creditFrames(index, focal / 10, 10);
    if (going && index >= 0) {
        space_.object(index).pos.z = camera_.z + focal;
    }
    going = going && creditFrames(index, 0, 0x32);
    drop(index);
    going = going && creditFrames(-1, 0, 0xF);
    if (going) {
        index = card("credgene");
        if (index >= 0) {
            space_.object(index).pos.z = camera_.z + focal;
        }
        session_.playEffect("GENEEULO");
        do {
            going = creditFrames(index, 0, 0xDC);
        } while (going && p.soundQueued[static_cast<int>(SoundChannel::Effect)] > 0.0);
        drop(index);
        going = going && creditFrames(-1, 0, 0xF);
    }
    drop(index);
    session_.stopEffects();
    session_.loadMusic("bridge");
    space_.watchFromBridge();
    p.pointerShown = true;
    setFullView(false);
    gameOver();
}

BridgeResult Bridge::run() {
    Platform& p = session_.platform;
    result_.reset();
    load();
    p.pointerShown = true;
    session_.loadMusic("bridge");
    setFullView(fullView_);
    setControl(true);
    while (!result_) {
        if (p.quit) {
            return BridgeResult{Leave::Quit, -1};
        }
        session_.waitTick();
        p.pointerShown = !voyage_.flyby;
        post({kTick, static_cast<int>(voyage_.clock)});
        ++voyage_.clock;
        handleInput();
        while (!events_.empty() && !result_) {
            const Event e = events_.front();
            events_.pop_front();
            dispatch(e);
        }
        if (!result_ && space_.playerDestroyed()) {
            shipLost();
        }
        drawFrame();
    }
    return *result_;
}

} // namespace helmsman::bridge
