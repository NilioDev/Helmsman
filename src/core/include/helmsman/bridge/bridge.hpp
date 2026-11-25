// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bridge of the first game: the crew at their stations, the viewscreen,
// and the menus each officer offers. A click on an officer, or a key, gives
// an order (an "action"); the mission being played may answer it itself,
// otherwise the bridge does what it always does. See docs/rules/bridge.md.
#pragma once

#include "helmsman/away/scene.hpp"
#include "helmsman/away/session.hpp"
#include "helmsman/away/ui.hpp"

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
    kSulu = 4,
    kChekov = 5,
    kUhura = 6,
    kScott = 7,
};

struct BridgeResult {
    bool quit = false;
    int loadSave = -1;
};

class Bridge {
public:
    explicit Bridge(away::Session& session);

    // Runs the bridge until the player quits or something leaves it.
    BridgeResult run();

    // For tools: one tick without the player.
    void advance();

    // An officer says something in a text box.
    int say(int officer, const std::string& text);
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
    void placeCrew(int officer, const std::string& anim, int x, int y);
    void updateCrew();
    void showFrame(CrewMember& c);
    void drawFrame();
    void tick();
    void handleInput();
    void leftClick();
    void setControl(bool on);
    void clampPointer();
    void doAction(int action);
    void defaultAction(int action);
    away::Ui ui();

    away::Session& session_;
    away::Scene scene_;
    std::array<CrewMember, 8> crew_{};
    away::Sprite light_;
    std::shared_ptr<const Bitmap> lightBitmap_;
    int lightStep_ = 0;
    std::uint64_t ticks_ = 0;
    bool control_ = false; // the pointer steers the ship in the viewscreen
    bool shieldsUp_ = false;
    bool weaponsArmed_ = false;
    bool targetAnalysis_ = false;
    std::deque<int> actions_;
    std::optional<BridgeResult> result_;
};

} // namespace helmsman::bridge
