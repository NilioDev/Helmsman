// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bridge of the first game: the crew at their stations, the viewscreen,
// and the menus each officer offers. Everything that happens on the bridge
// is an event: a tick of the clock, an order (an "action") from a click or
// a key, a report from the crew. The mission being played gets each event
// first and may answer it itself; what it leaves goes to the bridge, which
// does what it always does. See docs/rules/bridge.md.
#pragma once

#include "helmsman/away/scene.hpp"
#include "helmsman/away/session.hpp"
#include "helmsman/away/ui.hpp"
#include "helmsman/bridge/voyage.hpp"
#include "helmsman/space/space.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace helmsman::bridge {

// The orders, as the game numbers them.
enum Action : int {
    kKirkMenu = 0x00,
    kFirePhasers = 0x01,
    kFireTorpedoes = 0x02,
    kToggleViewscreen = 0x03,
    kCaptainsLog = 0x10,
    kTransporter = 0x11,
    kOptions = 0x12,
    kSpockTalk = 0x20,
    kComputer = 0x21,
    kRepairMenu = 0x30,
    kDamageReport = 0x31,
    kHail = 0x40,
    kOrbit = 0x50,
    kShields = 0x51,
    kStarMap = 0x60,
    kWeapons = 0x61,
    kRepairFirst = 0x70, // 0x70..0x76: which system to repair first
    kTargetAnalysis = 0x77,
};

// The officers, by their place in the game's list.
enum Officer : int {
    kKirk = 0,
    kSpock = 1,
    kMcCoy = 2,
    kSecurity = 3,
    kSulu = 4,
    kChekov = 5,
    kUhura = 6,
    kScott = 7,
};

// What happened, as the game numbers it.
enum EventType : int {
    kTick = 0,         // what: the clock when it was posted (one behind the voyage's)
    kOrder = 1,        // what: the Action
    kShieldsFalling = 2,
    kWeaponsOffLine = 3,
    kSpeedFalling = 4,
    kSaid = 5,         // a text box given a number closed; what: the number
    kBeamedUp = 6,     // the landing party is back from the planet
    kRepaired = 7,     // what: the system
};

struct Event {
    int type = kTick;
    int what = 0;
};

// Why the bridge was left.
enum class Leave {
    None,
    Quit,
    LoadSave,
    BeamDown,
    GameOver,
};

struct BridgeResult {
    Leave leave = Leave::None;
    int loadSave = -1;
    bool quit() const { return leave == Leave::Quit; }
};

class Bridge {
public:
    Bridge(away::Session& session, Voyage& voyage);

    // Runs the bridge until the player quits or something leaves it.
    BridgeResult run();

    // For tools: one tick without the player.
    void advance();

    space::Space& space() { return space_; }
    Voyage& voyage() { return voyage_; }
    away::Session& session() { return session_; }

    // --- What the missions use ---------------------------------------------

    // The bridge's own answer to an event.
    void defaultEvent(const Event& e);
    void post(Event e) { events_.push_back(e); }
    void clearEvents() { events_.clear(); }

    // A line of the mission's text file.
    std::string text(int index);
    // An officer says something in a text box; when `number` is above 0,
    // a kSaid event with it follows once the box is closed. Gives the
    // choice taken.
    int say(int officer, const std::string& text, int number = 0);
    // The same with the box's place and colour given.
    int sayAt(int officer, const std::string& text, int x, int y, int colour, int number = 0);
    // Someone not on the bridge, with a name of their own.
    int sayAs(const std::string& name, const std::string& text, int x, int y, int colour, int number = 0);
    // Someone on the viewscreen (an animation such as "woman") says
    // something, then the viewscreen shows space again.
    int comm(const std::string& picture, const std::string& name, const std::string& text);
    void showComm(const std::string& picture);
    void hideComm();
    // Kirk picks what to say from lines of the mission's text; gives the
    // place of the line picked.
    int choose(const std::vector<int>& lines);
    // The player types a code; empty when cancelled.
    std::string enterCode();
    // A ship that only drifts (4 "ark7", 5 "derelict", 6 "masada",
    // 7 "republic") comes into view as the Enterprise draws near and
    // takes up orbit beside it.
    void approach(int still);
    void removeStill();

    // An officer's place: their animation ("bstndsp", "xstndmc", ...) or
    // nobody.
    void placeCrew(int officer, const std::string& anim, int x = 0, int y = 0);
    void removeCrew(int officer);

    // Starts the mission with its title flying by: the title picture
    // (`title`.BMP), Kirk's words, and then `stage` of the mission.
    void flyby(const std::string& title, const std::string& voice, int stage);
    // Enters or leaves standard orbit around the planet here.
    void orbit(bool enter, bool announce);
    // Starfleet's verdict on a mission: `got` of `best` points.
    void score(int mission, int textIndex, int got, int best);
    // The ship's shields and weapons down, and its parts as good as new.
    void resetShip();
    // Removes every torpedo and blast from space.
    void clearShots();

    void music(int song);
    int song() const { return song_; }
    void sound(int number);
    void setControl(bool on);
    void setFullView(bool on);
    void beamDown() { result_ = BridgeResult{Leave::BeamDown, -1}; }

    // True while the ship is in orbit around a planet (and does not fly).
    bool inOrbit() const { return voyage_.inOrbit; }

    // The speaker's name and colour of an officer.
    static const char* speakerName(int officer);

private:
    struct CrewMember {
        bool present = false;
        const std::vector<away::AnimFrame>* frames = nullptr;
        int frame = 0;
        std::uint64_t nextTick = 0;
        int x = 0;
        int y = 0;
        away::Sprite sprite;
    };

    void load();
    void showFrame(CrewMember& c);
    void animate(CrewMember& c);
    void updateCrew();
    void drawFrame();
    void simulate();
    void handleInput();
    void leftClick();
    void clampPointer();
    void dispatch(const Event& e);
    void defaultAction(int action);
    void flybyEvent(const Event& e);
    void steer();
    void drawHud();
    void drawShipPanels(const space::Object& ship);
    void drawRadar();
    void panelPicture(const std::string& name, bool opaque, int recolour = -1);
    void setZoom(int zoom);
    void report(int event, int what, bool player);
    void starMap();
    void transporterMenu();
    away::Ui ui();

    away::Session& session_;
    Voyage& voyage_;
    away::Scene scene_;
    std::array<CrewMember, 8> crew_{};
    CrewMember comm_;
    away::Sprite light_;
    std::shared_ptr<const Bitmap> lightBitmap_;
    int lightStep_ = 0;
    std::uint64_t ticks_ = 0;
    bool control_ = false; // the pointer steers the ship in the viewscreen
    bool targetAnalysis_ = false;
    bool fullView_ = false; // the viewscreen fills the whole screen
    int zoom_ = 6;
    space::Space space_;
    away::Sprite radar_;
    std::shared_ptr<Bitmap> radarBitmap_;
    std::shared_ptr<const Bitmap> weaponLights_;
    away::Sprite title_;
    bool showTitle_ = false;
    int song_ = -1;
    std::deque<Event> events_;
    std::optional<BridgeResult> result_;
};

} // namespace helmsman::bridge
