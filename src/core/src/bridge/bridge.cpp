// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/bridge/bridge.hpp"

#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"

#include <algorithm>
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
    {"Captain Kirk", 0xB0, 0xA0, 0x82}, {"Mr. Spock", 0x2C, 0x126, 0x6A}, {"Dr. McCoy", 0xB0, 0x7A, 0x74},
    {"", 0xB0, 0x7A, 0x74},             {"Mr. Sulu", 0xB0, 0x7A, 0x74},  {"Mr. Chekov", 0xB0, 0xC4, 0x74},
    {"Lieutenant Uhura", 0xA1, 0x12A, 0x96}, {"Mr. Scott", 0xA1, 0x40, 0x64},
};

int randomInt(int below) {
    static std::mt19937 rng{std::random_device{}()};
    return below <= 1 ? 0 : static_cast<int>(rng() % static_cast<unsigned>(below));
}

} // namespace

Bridge::Bridge(away::Session& session) : session_(session) {
    scene_.font = &session_.font;
    load();
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
    placeCrew(kKirk, "bstndki", 0, 4);
    placeCrew(kSpock, "bstndsp", 0, 0);
    placeCrew(kUhura, "bstnduh", 0, 0);
    placeCrew(kChekov, "bstndch", 0, 0);
    placeCrew(kSulu, "bstndsu", 0, 0);
    placeCrew(kScott, "bstndsc", 0, 0);

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
    CrewMember& c = crew_[static_cast<std::size_t>(officer)];
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

void Bridge::updateCrew() {
    for (auto& c : crew_) {
        if (!c.present || c.frames == nullptr || c.frames->empty() || ticks_ < c.nextTick) {
            continue;
        }
        const int count = static_cast<int>(c.frames->size());
        const int next = (*c.frames)[static_cast<std::size_t>(std::min(c.frame, count - 1))].next[randomInt(4)];
        c.frame = next < count ? next : 0;
        showFrame(c);
        c.nextTick = ticks_ + static_cast<std::uint64_t>(std::max(1, (*c.frames)[static_cast<std::size_t>(c.frame)].ticks));
    }
}

void Bridge::drawFrame() {
    std::vector<const away::Sprite*> sprites;
    for (const auto& c : crew_) {
        if (c.present && c.sprite.bitmap) {
            sprites.push_back(&c.sprite);
        }
    }
    sprites.push_back(&light_);
    scene_.draw(session_.platform.screen, sprites);
}

void Bridge::tick() {
    ++ticks_;
    updateCrew();
    if ((ticks_ & 3) == 0) {
        lightStep_ = (lightStep_ + 1) % 22;
        light_.x = 0x49 + lightStep_ * 8;
    }
}

void Bridge::advance() {
    tick();
    drawFrame();
}

away::Ui Bridge::ui() {
    return away::Ui{session_, scene_, [this] { drawFrame(); }};
}

int Bridge::say(int officer, const std::string& text) {
    const Speaker& s = kSpeakers[std::clamp(officer, 0, 7)];
    away::Ui u = ui();
    const int r = away::textBox(u, {s.name, text}, s.x, s.y, s.colour);
    away::setPointer(session_, control_ ? (weaponsArmed_ ? "cursor" : "entcur") : "pushbtn");
    return r;
}

void Bridge::setControl(bool on) {
    control_ = on;
    Platform& p = session_.platform;
    away::setPointer(session_, control_ ? (weaponsArmed_ ? "cursor" : "entcur") : "pushbtn");
    p.warpPointer = true;
    p.warpX = 0xA0;
    p.warpY = control_ ? (kViewTop + kViewBottom) / 2 : 0xA6;
}

void Bridge::clampPointer() {
    Platform& p = session_.platform;
    int x = p.mouseX;
    int y = p.mouseY;
    if (control_) {
        x = std::clamp(x, kViewLeft, kViewRight);
        y = std::clamp(y, kViewTop, kViewBottom);
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
        actions_.push_back(kFirePhasers);
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
                actions_.push_back(choice);
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
    if ((clicks & 1) != 0) {
        leftClick();
    } else if ((clicks & 2) != 0 && control_) {
        actions_.push_back(kFireTorpedoes);
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
                actions_.push_back(kFirePhasers);
            } else {
                leftClick();
            }
            break;
        case scancode::kSpace:
        case 0x3C: // F2
            if (control_) {
                actions_.push_back(kFireTorpedoes);
            }
            break;
        case 0x11: actions_.push_back(kWeapons); break;        // W
        case 0x1F: actions_.push_back(kShields); break;        // S
        case 0x12: actions_.push_back(kRepairMenu); break;     // E
        case 0x20: actions_.push_back(kDamageReport); break;   // D
        case 0x1E: actions_.push_back(kTargetAnalysis); break; // A
        case 0x23: actions_.push_back(kHail); break;           // H
        case 0x14: actions_.push_back(kSpockTalk); break;      // T
        case 0x25: actions_.push_back(kKirkMenu); break;       // K
        case 0x18: actions_.push_back(kOrbit); break;          // O
        case 0x2F: actions_.push_back(kToggleViewscreen); break; // V
        case 0x31: actions_.push_back(kStarMap); break;        // N
        case 0x2E: actions_.push_back(kComputer); break;       // C
        case scancode::kEscape:
            actions_.push_back(kOptions);
            break;
        default:
            break;
        }
    }
}

void Bridge::doAction(int action) {
    // Missions answer here first, once they are played on the bridge.
    defaultAction(action);
}

void Bridge::defaultAction(int action) {
    switch (action) {
    case kKirkMenu: {
        away::Ui u = ui();
        const int choice = away::buttonMenu(u, "command", 60, 100, 0);
        away::setPointer(session_, control_ ? "entcur" : "pushbtn");
        if (choice >= 0) {
            actions_.push_back(choice);
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
        case 2: session_.setMusicOn(true); break;
        case 3: session_.setMusicOn(false); break;
        case 4: session_.effectsOn = true; break;
        case 5: session_.effectsOn = false; session_.stopEffects(); break;
        case 6: {
            away::Ui q = ui();
            if (away::textBox(q, {"Quit Game", "#GENE\\GENER028#Yes, quit the game.", "#GENE\\GENER008#No, do not quit the game."},
                              0x14, 0x14, 0xB0) == 0) {
                result_ = BridgeResult{true, -1};
            }
            break;
        }
        default: break;
        }
        break;
    }
    case kSpockTalk:
        say(kSpock, "#BRID\\B_155#Nothing to report, Captain.");
        break;
    case kHail:
        say(kUhura, "#BRID\\BRIDU016#Hailing frequencies open.");
        break;
    case kShields:
        shieldsUp_ = !shieldsUp_;
        say(kSulu, shieldsUp_ ? "#BRID\\B_340#Raising shields." : "#BRID\\B_337#Lowering shields, Captain.");
        break;
    case kWeapons:
        weaponsArmed_ = !weaponsArmed_;
        say(kChekov, weaponsArmed_ ? "#BRID\\B_351#Arming weapons." : "#BRID\\B_354#Disarming weapons.");
        break;
    case kTargetAnalysis:
        targetAnalysis_ = !targetAnalysis_;
        say(kSulu, targetAnalysis_ ? "#BRID\\B_344#Target Analysis On." : "#BRID\\B_343#Target Analysis Off.");
        break;
    case kTransporter:
        say(kSulu, "#BRID\\B_350#We're not in orbit, Captain.");
        break;
    case kOrbit:
        say(kSulu, "#BRID\\B_346#This isn't our destination.");
        break;
    case kRepairMenu: {
        away::Ui u = ui();
        const int choice = away::buttonMenu(u, "repair", 0x1E, 0x1E, 0);
        away::setPointer(session_, "pushbtn");
        if (choice >= 0) {
            actions_.push_back(choice);
        }
        break;
    }
    default:
        break;
    }
}

BridgeResult Bridge::run() {
    Platform& p = session_.platform;
    p.pointerShown = true;
    setControl(false);
    session_.loadMusic("bridge");
    session_.playMusic(0);
    while (!result_) {
        handleInput();
        while (!actions_.empty() && !result_) {
            const int a = actions_.front();
            actions_.pop_front();
            doAction(a);
        }
        if (p.quit) {
            return BridgeResult{true, -1};
        }
        session_.waitTick();
        tick();
        drawFrame();
    }
    return *result_;
}

} // namespace helmsman::bridge
