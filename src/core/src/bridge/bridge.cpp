// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/bridge/bridge.hpp"

#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"

#include <algorithm>
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
    {"Captain Kirk", 0xB0, 0xA0, 0x82}, {"Mr. Spock", 0x2C, 0x126, 0x6A}, {"Dr. McCoy", 0xB0, 0x7A, 0x74},
    {"", 0xB0, 0x7A, 0x74},             {"Mr. Sulu", 0xB0, 0x7A, 0x74},  {"Mr. Chekov", 0xB0, 0xC4, 0x74},
    {"Lieutenant Uhura", 0xA1, 0x12A, 0x96}, {"Mr. Scott", 0xA1, 0x40, 0x64},
};

int randomInt(int below) {
    static std::mt19937 rng{std::random_device{}()};
    return below <= 1 ? 0 : static_cast<int>(rng() % static_cast<unsigned>(below));
}

} // namespace

Bridge::Bridge(away::Session& session) : session_(session), space_(session.archive) {
    scene_.font = &session_.font;
    load();
    setZoom(4);
    space_.setPlayer("ENTERPRI");
    space_.report = [this](int event, int what, bool player) { report(event, what, player); };
    space_.sound = [this](int sound) {
        switch (sound) {
        case 0x24: session_.playEffect("PHASSHOT"); break;
        case 0x25: session_.playEffect("PHOTSHOT"); break;
        case 0x26: session_.playEffect("HITSHIEL"); break;
        default: break;
        }
    };
    space_.music = [this](int song) { session_.playMusic(song); };
}

void Bridge::setZoom(int zoom) {
    zoom_ = std::clamp(zoom, 4, 8);
    if (fullView_) {
        space_.setView(ClipRect{2, 2, 0x140, 0xC8}, zoom_);
    } else {
        space_.setView(ClipRect{kViewLeft, kViewTop, kViewRight + 1, kViewBottom + 1}, zoom_);
    }
}

void Bridge::report(int event, int, bool player) {
    if (!player) {
        return;
    }
    switch (event) {
    case 2: say(kScott, "#BRID\\BRID_S45#The shields are falling!"); break;
    case 3: say(kScott, "#BRID\\BRID_S37#Our weapons are off-line!"); break;
    case 4: say(kScott, "#BRID\\BRID_S47#We can't maintain our speed!"); break;
    default: break;
    }
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
    if (inOrbit) {
        return;
    }
    const Platform& p = session_.platform;
    if (control_) {
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
    if (fullView_) {
        space_.draw(session_.platform.screen);
        return;
    }
    space_.draw(scene_.backdrop);
    drawHud();
    std::vector<const away::Sprite*> sprites;
    if (radar_.bitmap) {
        sprites.push_back(&radar_);
    }
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
    steer();
    if (!inOrbit) {
        space_.tick();
    }
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
    away::setPointer(session_, control_ ? (space_.player().weaponsArmed ? "cursor" : "entcur") : "pushbtn");
    return r;
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
        case 0x29: // `: full speed backwards
            if (!inOrbit) space_.fullReverse();
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
            if (key.scancode >= 0x02 && key.scancode <= 0x0B && !inOrbit) {
                space_.setSpeedLevel(key.scancode - 0x02); // 1 stops, 0 is full speed
            }
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
    case kFirePhasers:
        if (!inOrbit) {
            const Platform& p = session_.platform;
            space_.firePhasers(control_ ? p.mouseX : space_.centreX(), control_ ? p.mouseY : space_.centreY());
        }
        break;
    case kFireTorpedoes:
        if (!inOrbit) {
            const Platform& p = session_.platform;
            space_.fireTorpedoes(control_ ? p.mouseX : space_.centreX(), control_ ? p.mouseY : space_.centreY());
        }
        break;
    case kToggleViewscreen:
        fullView_ = !fullView_;
        setZoom(zoom_);
        if (!fullView_) {
            setControl(control_);
        }
        break;
    case kShields: {
        bool& up = space_.player().shieldsUp;
        up = !up;
        say(kSulu, up ? "#BRID\\B_340#Raising shields." : "#BRID\\B_337#Lowering shields, Captain.");
        break;
    }
    case kWeapons: {
        bool& armed = space_.player().weaponsArmed;
        armed = !armed;
        say(kChekov, armed ? "#BRID\\B_351#Arming weapons." : "#BRID\\B_354#Disarming weapons.");
        break;
    }
    case kRepairFirst + 0: space_.repairFirst = 0x7FFF; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 1: space_.repairFirst = space::kPortPhasers; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 2: space_.repairFirst = space::kPortTorpedoes; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 3: space_.repairFirst = space::kSensors; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 4: space_.repairFirst = space::kBridge; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 5: space_.repairFirst = space::kHull; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
    case kRepairFirst + 6: space_.repairFirst = space::kImpulse; say(kScott, "#BRID\\BRID_S22#Aye Sir."); break;
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
