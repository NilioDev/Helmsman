// SPDX-License-Identifier: GPL-3.0-or-later
//
// Space on the second game's bridge: the viewscreen shows it when no one
// is on it, ships come and fight, and the crew's orders (shields, weapons,
// speed, the view, repairs) act on the player's ship. The fights are those
// of the first game (src/core/src/space); what differs is how the scripts
// call them and the crew's menus. See docs/rules/sequel-space.md.
#include "game_impl.hpp"

#include "helmsman/space/hud.hpp"

namespace helmsman::sequel::detail {

namespace {

// The viewscreen on the bridge picture, and the whole screen.
constexpr ClipRect kViewscreen{79, 31, 256, 104};
constexpr ClipRect kWholeScreen{2, 2, 0x140, 0xC8};

// The colours of the displays in the bridge's palette.
const space::HudColours kColours = [] {
    space::HudColours c;
    c.empty = 0xDE;
    c.weapon = {0xB1, 0xB2};
    c.power = 0xB1;
    c.boost = 0xA2;
    c.speedEmpty = 0xDF;
    c.wantSpeed = 0xA2;
    c.madeSpeed = 0xBA;
    c.radarCentre = 0x81;
    c.radarShips = {0xB8, 0xC8, 0xA2, 0xB3};
    return c;
}();

const std::vector<std::string> kPanelNames = {"ent", "klg", "ori", "rom", "fkr"};

// What the crew says.
constexpr Line kShieldsUp{4, "#b_340#Raising shields."};
constexpr Line kShieldsDown{4, "#b_337#Lowering shields."};
constexpr Line kArming{5, "#b_351#Arming weapons."};
constexpr Line kDisarming{5, "#b_354#Disarming weapons."};
constexpr Line kLocked{5, "#chk_001#Our weapons, are locked, Keptin."};
constexpr Line kNoEffect{5, "#chk_002#I do not think that would have any effect, Keptin."};
constexpr Line kYesKeptin{5, "#chk_003#Yes, Keptin."};
constexpr Line kAnalysisOn{4, "#b_344#Target Analysis On."};
constexpr Line kAnalysisOff{4, "#b_343#Target Analysis Off."};
constexpr Line kBoosting{7, "#BRID_S41#She can't take it, Captain."};
constexpr Line kBoosted{7, "#BRID_S32#I don't know how long she can take it, Captain."};
constexpr Line kOrbitFirst{4, "#b_349#We must first leave orbit, Sir."};
constexpr Line kShieldsFalling{7, "#brid_s45#The shields are falling!"};
constexpr Line kWeaponsOff{7, "#brid_s37#Our weapons are off-line!"};
constexpr Line kSpeedFalling{7, "#brid_s47#We can't maintain our speed!"};

// The parts the repair crew can be sent to first (damage control's
// buttons, in order), and what they are called when they are repaired.
constexpr int kRepairOrder[] = {0x7FFF, space::kPortPhasers, space::kPortTorpedoes, space::kSensors,
                                space::kBridge, space::kHull, space::kImpulse};

const char* partName(int part) {
    switch (part) {
    case space::kHull: return "Hull";
    case space::kBridge: return "Bridge";
    case space::kPortTorpedoes:
    case space::kStarboardTorpedoes: return "Photon torpedoes";
    case space::kPortPhasers:
    case space::kStarboardPhasers: return "Phasers";
    case space::kSensors: return "Sensors";
    case space::kImpulse: return "Impulse engines";
    case space::kPortWarp:
    case space::kStarboardWarp: return "Warp engines";
    default: return "Shields";
    }
}

} // namespace

void Game::setupSpace() {
    if (space_) {
        return;
    }
    space_ = std::make_unique<space::Space>(assets_.data());
    space_->useSequel([this](const std::string& name) -> std::optional<std::vector<std::uint8_t>> {
        if (episode_) {
            return episode_->load(upperName(name));
        }
        return std::nullopt;
    });
    space_->setPlayer("ENTERPRI");
    space_->report = [this](int event, int what, bool player) {
        if (player) {
            spaceEvents_.push_back({event, what});
        }
    };
    space_->sound = [this](int n) {
        if (n == 0x24) {
            playEffect("PHASSHOT");
        } else if (n == 0x25) {
            playEffect("PHOTSHOT");
        }
    };
    applyView();
}

// Where the space script starts an episode: no one about, the ship whole,
// shields and weapons down (`state`: also the bridge's own state of the
// fight, which a saved game brings instead).
void Game::resetSpace(bool state) {
    setupSpace();
    space_->clear();
    space_->setPlayer("ENTERPRI");
    // Each episode the admiral rated well makes the ship stronger: its
    // shields by half the rating, its weapons by a quarter.
    auto& cls = space_->player().cls;
    for (int e = 0; e < std::min<int>(m_.s16(ds::kEpisode), 8); ++e) {
        const int rating = std::clamp<int>(m_.s16(static_cast<std::uint16_t>(kRatings + 2 * e)), 0, 4);
        for (auto& sh : cls.shields) {
            sh.best += rating / 2;
            sh.now = sh.best;
        }
        for (int part = space::kPortTorpedoes; part <= space::kStarboardPhasers; ++part) {
            cls.parts[static_cast<std::size_t>(part)].best += rating / 4;
            cls.parts[static_cast<std::size_t>(part)].now = cls.parts[static_cast<std::size_t>(part)].best;
        }
    }
    space_->resetStars();
    spaceEvents_.clear();
    if (!state) {
        applyView();
        return;
    }
    m_.set16(fight::kFullView, 0);
    m_.set16(fight::kManual, 0);
    m_.set16(fight::kTargetAnalysis, 0);
    m_.set16(fight::kLocked, 0);
    m_.set16(fight::kLockTarget, 0xFFFF);
    m_.set16(fight::kLockStrength, 0);
    m_.set16(fight::kShields, 0);
    m_.set16(fight::kArmed, 0);
    m_.set16(fight::kWaveCount, 0);
    m_.set16(fight::kBoosted, 0);
    m_.set8(place::kUnderAttack, 0);
    spaceEvents_.clear();
    applyView();
}

int Game::zoom() const {
    const int z = m_.s16(fight::kZoom);
    return z >= 4 && z <= 8 ? z : 4;
}

void Game::applyView() {
    if (!space_) {
        return;
    }
    space_->setView(m_.u16(fight::kFullView) != 0 ? kWholeScreen : kViewscreen, zoom());
    space_->setLook(m_.s16(fight::kLook));
}

void Game::setManual(bool on) {
    m_.set16(fight::kManual, on ? 1 : 0);
    if (on) {
        p_.warpPointer = true;
        p_.warpX = space_->centreX();
        p_.warpY = space_->centreY();
    } else if (space_) {
        space_->steer(0, 0);
    }
    bridgePointer();
}

void Game::setFullView(bool on) {
    m_.set16(fight::kFullView, on ? 1 : 0);
    applyView();
}

void Game::bridgePointer() {
    const bool manual = m_.u16(fight::kManual) != 0;
    if (!manual) {
        cursorName_.clear();
        p_.pointer = Surface{0, 0};
        return;
    }
    setCursor(space_ && space_->player().weaponsArmed ? "cursor" : "entcur");
}

void Game::stepSpace() {
    if (!space_) {
        return;
    }
    const bool inOrbit = m_.u16(place::kInOrbit) != 0;
    if (m_.u16(fight::kManual) != 0 && !inOrbit) {
        space_->steer(p_.mouseX - space_->centreX(), p_.mouseY - space_->centreY());
        space_->setRoll(p_.heldKey == 0x52 ? 1 : (p_.heldKey == 0x53 ? -1 : 0));
    }
    if (!inOrbit) {
        space_->tick();
    }
    // Ships that were told to leave are gone once far enough; the others
    // fight. A wave keeps its number of fighting ships up.
    int fighting = 0;
    for (int i = 0; i < space::Space::kShips; ++i) {
        const space::Object& s = space_->object(space::Space::kFirstShip + i);
        if (s.kind == space::Kind::Ship && !s.destroyed && s.behaviour != 1) {
            ++fighting;
        }
    }
    const int wave = std::min<int>(m_.s16(fight::kWaveCount), space::Space::kShips);
    while (fighting < wave) {
        if (spawnShip(m_.text(fight::kWaveName, 10)) < 0) {
            break;
        }
        ++fighting;
    }
    m_.set8(place::kUnderAttack, fighting > 0 ? 1 : 0);
    trackLock();
}

// Weapons locked on: a ship ahead is taken as the target, and after
// 50 ticks of keeping it ahead the shots go straight at it.
void Game::trackLock() {
    if (m_.u16(fight::kLocked) == 0) {
        return;
    }
    const int target = m_.s16(fight::kLockTarget);
    const space::Object* t = enemy(target);
    if (t != nullptr && t->behaviour != 1 && space_->ahead(space::Space::kFirstShip + target)) {
        m_.set16(fight::kLockStrength, static_cast<std::uint16_t>(std::min(m_.s16(fight::kLockStrength) + 1, 50)));
        return;
    }
    m_.set16(fight::kLockStrength, 0);
    m_.set16(fight::kLockTarget, 0xFFFF);
    for (int i = 0; i < space::Space::kShips; ++i) {
        const space::Object* s = enemy(i);
        if (s != nullptr && s->behaviour != 1 && space_->ahead(space::Space::kFirstShip + i)) {
            m_.set16(fight::kLockTarget, static_cast<std::uint16_t>(i));
            return;
        }
    }
}

// Fires where the pointer is, or at the locked-on target.
void Game::fireWeapons(bool torpedoes) {
    const int target = m_.s16(fight::kLockTarget);
    if (m_.u16(fight::kLocked) != 0 && m_.s16(fight::kLockStrength) >= 50 && enemy(target) != nullptr) {
        space_->fireAt(space::Space::kFirstShip + target, torpedoes);
    } else if (torpedoes) {
        space_->fireTorpedoes(p_.mouseX, p_.mouseY);
    } else {
        space_->firePhasers(p_.mouseX, p_.mouseY);
    }
}

int Game::spawnShip(const std::string& name) {
    if (!space_ || name.empty()) {
        return -1;
    }
    try {
        const int index = space_->addShip(upperName(name));
        return index < 0 ? -1 : index - space::Space::kFirstShip;
    } catch (const FormatError&) {
        std::fprintf(stderr, "no ship called %s\n", name.c_str());
        return -1;
    }
}

space::Object* Game::enemy(int index) {
    if (!space_ || index < 0 || index >= space::Space::kShips) {
        return nullptr;
    }
    space::Object& s = space_->object(space::Space::kFirstShip + index);
    return s.kind == space::Kind::Ship ? &s : nullptr;
}

void Game::spaceReports() {
    while (!spaceEvents_.empty()) {
        const auto [event, what] = spaceEvents_.front();
        spaceEvents_.pop_front();
        switch (event) {
        case 2:
            sayLine(kShieldsFalling);
            break;
        case 3:
            sayLine(kWeaponsOff);
            break;
        case 4:
            sayLine(kSpeedFalling);
            break;
        case 7: {
            const std::string text = std::string(partName(what)) + " repaired, Captain.";
            say(7, {text}, 0);
            break;
        }
        default:
            break;
        }
    }
}

void Game::drawSpace(bool viewscreenFree) {
    if (!space_) {
        return;
    }
    if (m_.u16(fight::kFullView) != 0) {
        p_.screen.fill(0);
        space_->draw(p_.screen);
        return;
    }
    if (viewscreenFree) {
        space_->draw(p_.screen);
    }
    if (!weaponLights_) {
        weaponLights_ = picture("phasind");
    }
    space::drawHudBars(p_.screen, *space_, weaponLights_.get(), kColours);
    // Target analysis shows the target (the one locked on, else the last
    // one hit, else the first about) instead of our own ship.
    const space::Object* shown = &space_->player();
    int marked = -1;
    if (m_.u16(fight::kTargetAnalysis) != 0) {
        int target = enemy(m_.s16(fight::kLockTarget)) != nullptr ? space::Space::kFirstShip + m_.s16(fight::kLockTarget) : -1;
        if (target < 0 && space_->lastHit >= 0 && space_->object(space_->lastHit).kind == space::Kind::Ship) {
            target = space_->lastHit;
        }
        for (int i = 0; target < 0 && i < space::Space::kShips; ++i) {
            if (enemy(i) != nullptr) {
                target = space::Space::kFirstShip + i;
            }
        }
        if (target >= 0) {
            shown = &space_->object(target);
            marked = target;
        }
    }
    space::drawShipPanels(p_.screen, *shown, [this](const std::string& n) { return picture(n); }, kPanelNames);
    space::drawRadar(radar_, *space_, kColours, marked);
    for (int y = 0; y < radar_.height(); ++y) {
        for (int x = 0; x < radar_.width(); ++x) {
            if (const auto c = radar_.at(x, y); c != 0) {
                p_.screen.set(0x9F - 8 + x, 0x8C - 8 + y, c);
            }
        }
    }
}

bool Game::fullView() const { return m_.u16(fight::kFullView) != 0; }

// The crew's orders about the ship. False for those the space script or
// the bridge itself handles.
bool Game::combatButton(int id) {
    setupSpace();
    space::Object& me = space_->player();
    const bool inOrbit = m_.u16(place::kInOrbit) != 0;
    const bool attacked = m_.u8(place::kUnderAttack) != 0;
    auto submenu = [this](const char* name, int x, int y) {
        const int choice = chooseButtons(menu(name), x, y);
        if (choice >= 0) {
            bridgeButton(choice);
        }
    };
    switch (id) {
    case 0x00: // Kirk
        submenu("MNUKIRK", 0x3C, 0x64);
        return true;
    case 0x03: // the viewscreen alone, or the bridge again
        if (fullView()) {
            setFullView(false);
            setManual(false);
        } else {
            setFullView(true);
            setManual(true);
        }
        return true;
    case 0x22: { // Spock: target analysis
        const bool on = m_.u16(fight::kTargetAnalysis) == 0;
        m_.set16(fight::kTargetAnalysis, on ? 1 : 0);
        sayLine(on ? kAnalysisOn : kAnalysisOff);
        return true;
    }
    case 0x30: // Scotty: damage control
        submenu("MNUDMGC", 0x1E, 0x1E);
        return true;
    case 0x31: // Scotty: more power
        if (inOrbit) {
            sayLine(kOrbitFirst);
        } else if (m_.u16(fight::kBoosted) == 0) {
            m_.set16(fight::kBoosted, 1);
            me.powerBoost = std::clamp<int>(m_.s16(fight::kBoostTicks), 1, 18 * 120);
            sayLine(kBoosting);
        } else {
            sayLine(kBoosted);
        }
        return true;
    case 0x50: // Sulu: speed
        if (inOrbit) {
            sayLine(kOrbitFirst);
        } else {
            submenu("MNUSPEED", 0x3C, 0x64);
        }
        return true;
    case 0x52: // Sulu: shields
        me.shieldsUp = !me.shieldsUp;
        m_.set16(fight::kShields, me.shieldsUp ? 1 : 0);
        sayLine(me.shieldsUp ? kShieldsUp : kShieldsDown);
        return true;
    case 0x53: // Sulu: the view
        submenu("MNUVIEW", 0x3C, 0x64);
        return true;
    case 0x54:
        submenu("MNULOOK", 0x3C, 0x64);
        return true;
    case 0x55:
        submenu("MNUMAGN", 0x3C, 0x64);
        return true;
    case 0x56: // the viewscreen, steered by hand
        if (!fullView()) {
            setFullView(true);
            setManual(true);
        }
        return true;
    case 0x61: // Chekov: weapons
        me.weaponsArmed = !me.weaponsArmed;
        m_.set16(fight::kArmed, me.weaponsArmed ? 1 : 0);
        sayLine(me.weaponsArmed ? kArming : kDisarming);
        bridgePointer();
        return true;
    case 0x62: { // Chekov: lock the weapons on
        const bool on = m_.u16(fight::kLocked) == 0;
        m_.set16(fight::kLockTarget, 0xFFFF);
        m_.set16(fight::kLockStrength, 0);
        if (!on) {
            m_.set16(fight::kLocked, 0);
            sayLine(kYesKeptin);
        } else if (attacked) {
            m_.set16(fight::kLocked, 1);
            me.weaponsArmed = true;
            m_.set16(fight::kArmed, 1);
            sayLine(kLocked);
        } else {
            sayLine(kNoEffect);
        }
        return true;
    }
    default:
        break;
    }
    if (id >= 0x70 && id <= 0x76) {
        space_->repairFirst = kRepairOrder[id - 0x70];
        return true;
    }
    if (id >= 0x80 && id <= 0x85) {
        // Left, right, ahead, behind, up, down.
        static constexpr int kLooks[] = {2, 3, 0, 1, 4, 5};
        m_.set16(fight::kLook, static_cast<std::uint16_t>(kLooks[id - 0x80]));
        applyView();
        return true;
    }
    if (id >= 0x90 && id <= 0x94) {
        m_.set16(fight::kZoom, static_cast<std::uint16_t>(id - 0x8C));
        applyView();
        char text[32];
        std::snprintf(text, sizeof text, "Magnification %d", id - 0x8F);
        say(4, {text}, 0);
        return true;
    }
    if (id == 0xA0) {
        if (!inOrbit) {
            space_->fullReverse();
        }
        return true;
    }
    if (id >= 0xA1 && id <= 0xAA) {
        if (!inOrbit) {
            space_->setSpeedLevel(id - 0xA1);
        }
        return true;
    }
    return false;
}

// Keys on the bridge, as the original has them. True when the key was
// one of them.
bool Game::combatKey(const KeyPress& k) {
    setupSpace();
    const bool manual = m_.u16(fight::kManual) != 0;
    switch (k.scancode) {
    case scancode::kTab: setManual(!manual); return true;
    case 0x11: bridgeButton(0x61); return true; // W: weapons
    case 0x12: bridgeButton(0x31); return true; // E: more power
    case 0x13: bridgeButton(0x10); return true; // R: the captain's log
    case 0x14: bridgeButton(0x20); return true; // T: Spock
    case 0x17: bridgeButton(0x12); return true; // I: the options
    case 0x18: bridgeButton(0x51); return true; // O: orbit
    case 0x1E: bridgeButton(0x22); return true; // A: target analysis
    case 0x1F: bridgeButton(0x52); return true; // S: shields
    case 0x20: bridgeButton(0x30); return true; // D: damage control
    case 0x23: bridgeButton(0x40); return true; // H: hail
    case 0x25: bridgeButton(0x00); return true; // K: Kirk
    case 0x26: bridgeButton(0x62); return true; // L: lock weapons on
    case 0x2E: bridgeButton(0x21); return true; // C: the library computer
    case 0x2F: bridgeButton(0x03); return true; // V: the viewscreen
    case 0x30: bridgeButton(0x11); return true; // B: beam down
    case 0x31: bridgeButton(0x60); return true; // N: course
    case 0x1A: // [ and ]: slower, faster
    case 0x1B: {
        const space::Object& me = space_->player();
        const std::int32_t maxSpeed = std::max<std::int32_t>(1, me.cls.maxSpeed);
        const int level = static_cast<int>(static_cast<std::int64_t>(me.wantSpeed) * 9 / maxSpeed);
        if (m_.u16(place::kInOrbit) == 0) {
            space_->setSpeedLevel(std::clamp(level + (k.scancode == 0x1B ? 1 : -1), 0, 9));
        }
        return true;
    }
    case 0x29: // `: full speed backwards
        bridgeButton(0xA0);
        return true;
    case 0x33: // , and .: magnification
    case 0x34: {
        const int z = std::clamp(zoom() + (k.scancode == 0x34 ? 1 : -1), 4, 8);
        if (z != zoom()) {
            bridgeButton(0x8C + z);
        }
        return true;
    }
    case scancode::kEnter:
    case scancode::kF1:
        if (manual) {
            fireWeapons(false);
            return true;
        }
        return false;
    case scancode::kSpace:
    case 0x3C: // F2
        if (manual) {
            fireWeapons(true);
        }
        return true;
    case 0x4C: // keypad 5: stop turning
        space_->steer(0, 0);
        space_->setRoll(0);
        if (manual) {
            p_.warpPointer = true;
            p_.warpX = space_->centreX();
            p_.warpY = space_->centreY();
        }
        return true;
    default:
        if (k.scancode >= 0x3D && k.scancode <= 0x42) { // F3..F8: look about
            m_.set16(fight::kLook, static_cast<std::uint16_t>(k.scancode - 0x3D));
            applyView();
            return true;
        }
        return false;
    }
}

// A click while steering by hand: phasers (left) or torpedoes (right).
bool Game::combatClick(std::uint8_t clicks) {
    if (!space_ || m_.u16(fight::kManual) == 0) {
        return false;
    }
    if ((clicks & 1) != 0) {
        fireWeapons(false);
    }
    if ((clicks & 2) != 0) {
        fireWeapons(true);
    }
    return true;
}

// The space script's commands about ships.
std::uint32_t Game::shipCommand(ScriptCall& c) {
    setupSpace();
    const auto seg = Machine::kSpace;
    switch (c.function) {
    case 0x01: { // a ship joins: (names); its number
        // Two names follow each other: the ship, and the easier one for
        // the lower combat levels.
        const auto at = c.arg(0);
        std::string name = m_.scriptText(seg, at, 20);
        if (m_.s16(ds::kCombatLevel) < 2) {
            const std::string easy = m_.scriptText(seg, static_cast<std::uint16_t>(at + name.size() + 1), 20);
            if (!easy.empty()) {
                name = easy;
            }
        }
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(spawnShip(name))));
    }
    case 0x02: // ships keep coming: (name, how many at once)
        m_.setText(fight::kWaveName, m_.scriptText(seg, c.arg(0), 10), 10);
        m_.set16(fight::kWaveCount, static_cast<std::uint16_t>(std::clamp<int>(c.sarg(1), 0, 4)));
        return 0;
    case 0x03: // a ship gives up and leaves
        if (space::Object* s = enemy(c.sarg(0))) {
            s->behaviour = 1;
            s->weaponsArmed = false;
        }
        return 0;
    case 0x04: { // has a ship lost half its hull (-2: ours)?
        const int i = c.sarg(0);
        if (i == -2) {
            const auto& hull = space_->player().cls.parts[space::kHull];
            return hull.best / 2 > hull.now ? 1 : 0;
        }
        if (i < 0 || i >= space::Space::kShips) {
            return 0;
        }
        const space::Object& s = space_->object(space::Space::kFirstShip + i);
        // A ship blown up has lost it too (the original forgets it, and a
        // script waiting on it would wait for ever).
        if (s.kind != space::Kind::Ship) {
            return 0;
        }
        if (s.destroyed) {
            return 1;
        }
        const auto& hull = s.cls.parts[space::kHull];
        return s.behaviour != 1 && hull.best / 2 > hull.now ? 1 : 0;
    }
    case 0x05: { // are a ship's shields below two thirds (-2: ours)?
        const int i = c.sarg(0);
        const space::Object* s = i == -2 ? &space_->player() : enemy(i);
        if (s == nullptr || (i != -2 && s->behaviour == 1)) {
            return 0;
        }
        int best = 0;
        int now = 0;
        for (const auto& sh : s->cls.shields) {
            best += sh.best;
            now += sh.now;
        }
        return best * 2 > now * 3 ? 1 : 0;
    }
    case 0x06: // the station or wreck goes
        for (int i = space::Space::kFirstStill; i < space::Space::kFirstStill + space::Space::kStills; ++i) {
            space_->remove(i);
        }
        return 0;
    case 0x2B: // shields up
        space_->player().shieldsUp = true;
        m_.set16(fight::kShields, 1);
        return 0;
    case 0x2C: // weapons armed
        space_->player().weaponsArmed = true;
        m_.set16(fight::kArmed, 1);
        bridgePointer();
        return 0;
    case 0x10: // full speed ahead
        space_->setSpeedLevel(9);
        return 0;
    default:
        return 0;
    }
}

} // namespace helmsman::sequel::detail
