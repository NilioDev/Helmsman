// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's own pieces, shared by its bridge (game.cpp) and its
// away missions (away.cpp). Internal to the core library.
#pragma once

#include "helmsman/sequel/game.hpp"
#include "helmsman/away/assets.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/data/sound_bank.hpp"
#include "helmsman/engine/pad_buttons.hpp"
#include "helmsman/engine/savegame.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/sequel/machine.hpp"
#include "helmsman/sequel/text.hpp"
#include "helmsman/space/space.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <deque>
#include <array>

#include "helmsman/away/scene.hpp"

namespace helmsman::sequel::detail {


// The original counts time in ticks of the PC's clock: 18.2 a second.
constexpr double kTickMillis = 1000.0 / 18.2065;

// The crew, by their place in the list of people (DS:73D8).
constexpr int kCrew = 8;
// Which of them stand on the bridge, and their standing animations.
struct Station {
    int actor;
    const char* anim;
};
constexpr Station kStations[] = {{0, "BSTNDKI"}, {1, "BSTNDSP"}, {6, "BSTNDUH"},
                                 {5, "BSTNDCH"}, {4, "BSTNDSU"}, {7, "BSTNDSC"}};

// Where the viewscreen's pictures go.
constexpr int kViewscreenX = 0x47;
constexpr int kViewscreenY = 0x1E;

// The star systems on the star map: 20 places (x, y).
constexpr std::uint16_t kSystems = 0x236C;
constexpr int kSystemCount = 20;

// Where the crew's menus are listed in the data segment: a name of ten
// bytes and where the menu opens, for each of the eight.
constexpr std::uint16_t kMenuTable = 0x025E;
constexpr std::uint16_t kMenuEntry = 14;
// The text colour sets.
constexpr std::uint16_t kTextColours = 0x22FE;
constexpr std::uint16_t kRestoreCode = 0x4FF8; // what a restored checkpoint tells the script

// Places in a person's record (0x8A bytes).
namespace actor {
constexpr std::uint16_t kFlags = 0x00;
constexpr std::uint16_t kX = 0x0C;
constexpr std::uint16_t kY = 0x0E;
constexpr std::uint16_t kTeam = 0x45;
constexpr std::uint16_t kName = 0x4B;
constexpr std::uint16_t kTextSet = 0x63;
constexpr std::uint16_t kTextX = 0x64;
constexpr std::uint16_t kTextY = 0x66;
} // namespace actor

// Who a line is said by when it is not one of the people.
constexpr int kViewscreenSpeaker = -3;
constexpr int kChoiceSpeaker = -2;

// The bridge's own events (the button numbers of the crew's menus).
namespace button {
constexpr int kKirkBeam = 0x11;
constexpr int kKirkOptions = 0x12;
constexpr int kSpockReport = 0x20;
constexpr int kUhuraHail = 0x40;
constexpr int kSuluOrbit = 0x51;
constexpr int kChekovCourse = 0x60;
} // namespace button

// Where an event loop keeps its handlers: word offsets from the resume
// point.
namespace handler {
constexpr int kHail = 0;
constexpr int kReport = 2;
constexpr int kTick = 4;
constexpr int kArrived = 6;
constexpr int kOrbit = 0x0A;
constexpr int kBeam = 0x0C;
} // namespace handler

namespace cmd {
constexpr int kTitles = 0x00;
constexpr int kEndTitles = 0x07;
constexpr int kViewscreen = 0x08;
constexpr int kViewscreenOn = 0x09;
constexpr int kViewscreenOff = 0x0A;
constexpr int kSetDestination = 0x0C;
constexpr int kEnterOrbit = 0x0D;
constexpr int kLeaveOrbit = 0x0E;
constexpr int kSay = 0x16;
constexpr int kPause = 0x17;
constexpr int kEventLoop = 0x18;
constexpr int kAway = 0x19;
constexpr int kEpisodeOver = 0x1A;
constexpr int kRandom = 0x1B;
constexpr int kMusic = 0x1C;
constexpr int kSong = 0x1D;
constexpr int kShowCrew = 0x26;
constexpr int kHideCrew = 0x27;
constexpr int kCrewFlags = 0x28;
constexpr int kScene = 0x2D;
constexpr int kWait = 0x30;
constexpr int kCheckpoint = 0x33;
constexpr int kBackToCheckpoint = 0x34;
constexpr int kRestored = 0x35;
constexpr int kSpeakerAnim = 0x38;
constexpr int kOpenBank = 0x39;
constexpr int kCloseBanks = 0x3A;
} // namespace cmd

// Places in the data segment this file uses.
namespace place {
constexpr std::uint16_t kInOrbit = 0x00C4;
constexpr std::uint16_t kUnderAttack = 0xC220;
constexpr std::uint16_t kPauseTicks = 0x5EBE;
} // namespace place

// Where the bridge keeps the state of a fight (the original's places, so
// that saved games hold them).
namespace fight {
constexpr std::uint16_t kFullView = 0x00C0;       // the viewscreen fills the screen
constexpr std::uint16_t kManual = 0x00C2;         // the ship is steered by hand
constexpr std::uint16_t kZoom = 0x00C8;           // magnification, 4..8
constexpr std::uint16_t kLook = 0x00CA;           // which way the view looks (0..5)
constexpr std::uint16_t kTargetAnalysis = 0x0104;
constexpr std::uint16_t kLocked = 0x0108;         // the weapons are locked on targets
constexpr std::uint16_t kLockTarget = 0x010A;     // the target (-1 none)
constexpr std::uint16_t kLockStrength = 0x010C;   // ticks it has been ahead, up to 50
constexpr std::uint16_t kShields = 0xC47E;
constexpr std::uint16_t kArmed = 0xC482;
constexpr std::uint16_t kWaveCount = 0xBF70;      // ships that keep coming, how many at once
constexpr std::uint16_t kWaveName = 0xC84A;       // and their kind
constexpr std::uint16_t kBoosted = 0x6E96;        // Scotty's extra power was given
constexpr std::uint16_t kBoostTicks = 0x6EA0;     // and for how long
} // namespace fight

// Default lines of the bridge crew (as the original says them when the
// script has nothing to add).
struct Line {
    int who;
    const char* text;
};
constexpr Line kNothingToReport{1, "#B_155#Nothing to report, Captain."};
constexpr Line kHailing{6, "#BRIDU016#Hailing frequencies open."};
constexpr Line kNotInOrbit{4, "#B_350#We're not in orbit Captain."};
constexpr Line kRightNow{4, "Right now, Captain?"};
constexpr Line kUnderAttack{1, "#B_348#Unable to comply, Captain. We're under attack."};
constexpr Line kNotDone{1, "#B_338#May I respectfully remind the Captain that we haven't accomplished our mission, Sir."};
constexpr Line kOrbiting{4, "#B_333#Entering standard orbit."};
constexpr Line kLeaveOrbitFirst{4, "#B_349#We must first leave orbit, Sir."};

inline std::string lowerName(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::string upperName(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// A picture playing an animation (.ANM of 22-byte frames).
struct Sprite {
    std::string name;
    std::vector<away::AnimFrame> frames;
    std::size_t frame = 0;
    double nextAt = 0;
    bool shown = false;
    int x = 0; // where it stands
    int y = 0;
    bool still = false; // shows its first frame only
};

// A person or thing in a room, as Helmsman shows it. What the scripts see
// of it lives in its record in the data segment (DS:73D8 + 0x8A each).
struct Body {
    bool used = false;
    std::string anim; // the animation's name as asked for
    const std::vector<away::AnimFrame>* frames = nullptr;
    std::size_t frame = 0;
    std::uint64_t nextAt = 0; // tick of the next frame
    int x = 0; // where it stands
    int y = 0;
    int scale = 256;
    away::Sprite sprite;
    // Walking: the points still to go to, the place now (16.16).
    bool walking = false;
    std::deque<std::pair<int, int>> path;
    std::int32_t fx = 0;
    std::int32_t fy = 0;
    std::int32_t vx = 0;
    std::int32_t vy = 0;
    int steps = 0;
    int walkFrame = 0;
    char dir = 's';
    int code = 0; // the event code when it is done (0 none)
    bool held = false; // the last frame of a finished animation stays
};

// A room's polygon: an id and its corners.
struct Polygon {
    int id = 0;
    std::vector<std::pair<int, int>> points;
    bool contains(int x, int y) const;
};

// The mission is left: for the bridge (beamed up), or a new room.
struct LeaveMission {
    int state;
};
struct ChangeRoom {
    int room;
    int spawn;
};

// Back to a saved game (a checkpoint's restore code, or -1), or a new one.
struct RestoreGame {
    std::vector<std::uint8_t> payload;
    int code = -1;
};
struct RestartGame {};

struct MenuButton {
    std::string picture;
    int x = 0;
    int y = 0;
    int id = 0;
};

class Game {
public:
    Game(Platform& p, Assets& assets)
        : p_(p), assets_(assets), m_(needProgram(assets.source())),
          font_(SequelFont::fromBytes(assets.data().need("FONT.FON"))) {
        std::vector<std::uint8_t> colours(SequelFont::kColours * 8);
        for (std::size_t i = 0; i < colours.size(); ++i) {
            colours[i] = m_.u8(static_cast<std::uint16_t>(kTextColours + i));
        }
        font_.setColours(colours);
        rng_.seed(static_cast<unsigned>(p.millis));
    }

    void run();

private:
    static std::vector<std::uint8_t> needProgram(GameSource& source) {
        auto program = source.read(programName(source.game()));
        if (!program) {
            throw FormatError("the game's program is missing");
        }
        return std::move(*program);
    }

    // Files.
    std::optional<std::vector<std::uint8_t>> file(std::string_view name) const;
    std::shared_ptr<const Bitmap> picture(std::string_view name);
    const std::vector<away::AnimFrame>& anim(std::string_view name);
    std::vector<MenuButton> menu(std::string_view name);

    // Episodes.
    // `fresh`: the crew starts the episode anew (not so for a saved game).
    void startEpisode(bool fresh = true);
    void resetCrew();

    // The bridge.
    void bridge();
    void loadBridge();
    void tickSprites();
    void drawSprite(const Sprite& s);
    void drawBridge();
    void present();
    void setSprite(Sprite& s, std::string_view anim);
    int crewAt(int x, int y);
    int chooseFromMenu(int who);
    int starMap();
    void bridgeButton(int id);

    // Time.
    void waitTicks(int ticks);
    void pumpTicks();

    // The space script.
    void resumeSpace(std::uint16_t at);
    bool handle(int offset);
    Machine::Flow spaceCommand(ScriptCall& call, std::uint32_t& result);
    int sayStruct(std::uint16_t seg, std::uint16_t at);

    // Lines and choices.
    int say(int who, const std::vector<std::string>& texts, std::uint16_t seg);
    // A menu of the game's own: a title and choices; -1 when the player
    // backs out (Escape, the right button).
    int menuBox(const std::string& title, const std::vector<std::string>& choices);
    // Set by menuBox for the length of one say().
    const std::string* menuTitle_ = nullptr;
    void sayLine(const Line& line) { say(line.who, {line.text}, 0); }
    void playVoice(const std::string& tag);

    // Space and fights (combat.cpp).
    void setupSpace();
    void resetSpace(bool state = true);
    void stepSpace();
    void trackLock();
    void fireWeapons(bool torpedoes);
    void drawSpace(bool viewscreenFree);
    void spaceReports();
    bool combatButton(int id);
    bool combatKey(const KeyPress& key);
    bool combatClick(std::uint8_t clicks);
    std::uint32_t shipCommand(ScriptCall& call);
    int spawnShip(const std::string& name);
    space::Object* enemy(int index);
    void setManual(bool on);
    void setFullView(bool on);
    bool fullView() const;
    void applyView();
    int zoom() const;
    void bridgePointer();
    std::unique_ptr<space::Space> space_;
    std::shared_ptr<const Bitmap> weaponLights_;
    Surface radar_{17, 17};
    std::deque<std::pair<int, int>> spaceEvents_;
    // While above 0, space stands still (a menu is open).
    int holdSpace_ = 0;
    struct HoldSpace {
        explicit HoldSpace(Game& g) : game(g) { ++game.holdSpace_; }
        ~HoldSpace() { --game.holdSpace_; }
        HoldSpace(const HoldSpace&) = delete;
        HoldSpace& operator=(const HoldSpace&) = delete;
        Game& game;
    };

    // Away missions (away.cpp).
    void awayMission();
    // `restoring`: for a saved game, whose data segment already holds what
    // a room's entry would set.
    void loadRoom(int room, bool first, bool restoring = false);
    void loadBackdrop(const std::string& name);
    void loadDepths(const std::string& name);
    // Saved games (saves.cpp).
    std::vector<std::uint8_t> savePayload();
    void writeAway(SaveWriter& w) const;
    void readAway(SaveReader& r);
    void restorePayload(std::span<const std::uint8_t> payload);
    void saveMenu();
    bool anySaves() const;
    bool loadMenu();
    void writeCheckpoint(const std::string& name);
    void newGame();
    void startMenu();
    // How hard the fights are (0 none, 1 easy, 2 hard), as asked before a
    // new game; -1 as the new game's file has it.
    int combatLevel_ = -1;
    void devAwayMission();
    bool devStart();
    void episodes();
    std::optional<std::vector<std::uint8_t>> readCheckpoint(const std::string& name);
    int chooseButtons(const std::vector<MenuButton>& buttons, int x, int y);
    bool soundOn_ = true;
    bool musicOn_ = true;
    bool optionsMenu();
    void gameOverMenu();
    // The names of what the room shows now (a room may change them).
    std::string backdropName_;
    std::string depthsName_;
    std::string walkMapName_;
    // A saved game to go on from, read and waiting for the bridge loop or
    // an away mission to pick it up.
    std::optional<std::vector<std::uint8_t>> resumeAway_;
    void enterRoom(int spawn);
    void awayTick();
    void awayFrame(); // one frame while the scripts wait: animate, draw
    void advanceBodies();
    void drawAway();
    void drawScene() { away_ ? drawAway() : drawBridge(); }
    void post(std::uint8_t type, std::uint8_t a = 0, std::uint8_t b = 0, std::uint8_t c = 0);
    bool runList(int list, const std::uint8_t event[4], bool run);
    bool sendEvent(bool quiet, bool noFallback, const std::uint8_t event[4]);
    void dispatchQueued();
    std::uint32_t roomCommand(ScriptCall& call);
    std::uint16_t handlerSeg(int kind) const;
    void pushHandlers(int kind, std::uint16_t table, int priority);
    void popHandlers(int kind, int table);
    std::uint16_t record(int who) const { return static_cast<std::uint16_t>(ds::kActors + who * ds::kActorSize); }
    std::uint8_t rec8(int who, std::uint16_t at) const { return m_.u8(static_cast<std::uint16_t>(record(who) + at)); }
    void setRec8(int who, std::uint16_t at, std::uint8_t v) { m_.set8(static_cast<std::uint16_t>(record(who) + at), v); }
    int startAnim(int who, std::string name, int x, int y, int scale);
    void stand(int who);
    bool walk(int who, int fx, int fy, int tx, int ty, int code);
    void walkStep(Body& b, int who);
    void nextLeg(Body& b);
    void setWalkPicture(Body& b, int who);
    void finish(int who);
    void waitDone(int who);
    void waitAwayTicks(int ticks);
    void placeSprite(Body& b);
    int scaleAt(int y) const;
    int depthOfRow(int y) const { return scene_.depthAt(0, std::clamp(y, 0, 199)); }
    bool blocked(int x, int y) const;
    bool lineClear(int x0, int y0, int x1, int y1) const;
    int nearestNode(int x, int y) const;
    void loadWalkMap(const std::string& name);
    std::vector<Polygon> polygons(std::uint16_t from, std::uint16_t to) const;
    int thingAt(int x, int y);
    void checkAreas(int x, int y);
    void awayInput();
    int actionMenu();
    int inventory(bool forUse);
    void setCursor(const std::string& name);
    std::string roomText(std::uint16_t off, std::size_t max = 256) const;
    int sayRoom(std::uint16_t at);
    void playEffect(const std::string& name);
    int askText();

    std::shared_ptr<const Bitmap> scaledPicture(const std::string& name, int scale);
    std::map<std::pair<std::string, int>, std::shared_ptr<const Bitmap>> scaled_;
    // Lines the rooms draw (phaser beams): up to six.
    struct Beam {
        bool used = false;
        std::shared_ptr<Bitmap> bitmap;
        away::Sprite sprite;
    };
    Beam beams_[6];
    bool away_ = false;
    away::Scene scene_;
    Palette roomPalette_;
    // The room's brightness in percent (DS:CAAA) and where it is going; it
    // fades towards fadeColour_ (DS:11B2), 10 percent a tick.
    int fade_ = 100;
    int fadeTarget_ = 100;
    std::uint64_t fadeTick_ = 0;
    Colour fadeColour_{};
    void fadeTo(int target, bool wait);
    struct Ban {
        std::vector<std::uint8_t> data;
        std::size_t pos = 0;
    };
    std::vector<Ban> bans_;
    void stepRoomClock();
    void stepBan(Ban& ban);
    // A panel of buttons a room puts up (a puzzle): its menu and where.
    std::vector<MenuButton> panel_;
    int panelX_ = 0;
    int panelY_ = 0;
    int panelHover();
    void drawPanel();
    int pollPanel(std::int32_t ticks);
    Body bodies_[32];
    std::vector<std::uint8_t> walkMap_;
    std::vector<std::pair<int, int>> nodes_;
    std::vector<std::uint8_t> nextHop_;
    std::deque<std::array<std::uint8_t, 4>> queue_;
    std::uint64_t awayTicks_ = 0;  // ticks since the mission began (animations)
    std::uint32_t roomTicks_ = 0;  // ticks since the room was entered (tick events)
    int roomNumber_ = 0;
    std::uint16_t currentSeg_ = 0; // the code a running handler belongs to
    int currentKind_ = 0;
    int area_ = -1; // the touch area Kirk stands in
    int exitArmed_ = -1;
    int action_ = 1; // 1 walk 2 use 3 get 4 look 5 talk
    int subject_ = -1; // what is being used
    bool busy_ = false; // the crew is beaming in or the like: no orders
    bool awayPending_ = false;
    std::shared_ptr<const Bitmap> cursor_;
    std::string cursorName_;
    std::string talkingAnim_;
    int talker_ = -1;
    bool speaking_ = false;
    std::string typedText_;

    Platform& p_;
    Assets& assets_;
    Machine m_;
    SequelFont font_;
    std::mt19937 rng_;

    std::string episodeName_;
    std::optional<Archive> episode_;
    std::optional<SoundBank> voices_;
    std::map<std::string, std::shared_ptr<const Bitmap>> pictures_;
    std::map<std::string, std::vector<away::AnimFrame>> anims_;

    Palette palette_;
    Surface background_;
    std::array<std::uint8_t, 256> shade_{};
    Sprite crew_[kCrew];
    Sprite talk_[kCrew];
    Sprite viewscreen_;
    bool viewscreenTalks_ = false;
    int pendingTalker_ = -100;
    std::string pendingTalkAnim_;
    double nextTick_ = 0;
    int ticksDue_ = 0;
    bool leaveBridge_ = false;
};

} // namespace helmsman::sequel::detail
