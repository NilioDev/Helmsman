// SPDX-License-Identifier: GPL-3.0-or-later
//
// A planet mission ("away mission") of the first game: Kirk, Spock, McCoy
// and a security officer in a room of a planet or ship, walking, looking,
// talking and using things. Each room's file (.RDF) holds its own actions
// as small pieces of x86 code; they run in Helmsman's interpreter and call
// back into the functions here. The rest is Helmsman's own work, built to
// behave as the original does. See docs/rules/away-missions.md.
#pragma once

#include "helmsman/away/scene.hpp"
#include "helmsman/away/session.hpp"
#include "helmsman/away/ui.hpp"
#include "helmsman/cpu/x86.hpp"
#include "helmsman/engine/savegame.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace helmsman::away {

// Actions, as the rooms' action lists name them.
enum Action : std::uint8_t {
    kTick = 0,
    kWalk = 1,
    kUse = 2,
    kGet = 3,
    kLook = 4,
    kTalk = 5,
    kTouchedWarp = 6,
    kTouchedHotspot = 7,
    kTimerExpired = 8,
    kFinishedAnimation = 10,
    kFinishedWalking = 12,
    kOptions = 13,
};

struct Event {
    std::uint8_t action = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0;
    std::uint8_t b3 = 0;
};

// One of the 32 things that move in a room: the crew (0..3) and whatever
// the room adds.
struct Actor {
    bool used = false;
    enum class State { Standing, Walking, Animating } state = State::Standing;
    std::string animName;
    Sprite sprite;
    std::string pictureName;
    int scale = 256;
    const std::vector<AnimFrame>* frames = nullptr;
    int frame = 0;
    std::uint64_t nextTick = 0;
    int x = 0;
    int y = 0;
    int loops = 0;
    bool notifyFinish = false;
    int finishEvent = 0;
    // Walking.
    std::string walkName;
    int pathNode = -1;
    int destNode = -1;
    std::int32_t fx = 0; // 16.16 fixed point
    std::int32_t fy = 0;
    std::int32_t vx = 0;
    std::int32_t vy = 0;
    int destX = 0;
    int destY = 0;
    int steps = 0;
    int walkFrame = 0;
    char dir = 0;
    std::string standName;
};

// How a mission ends, for the game to carry on from.
struct MissionResult {
    int score = 0;        // the commendation points the room code gave
    int a = 0;
    int b = 0;
    bool quit = false;    // the player left the game
    int loadSave = -1;    // the player chose to load this saved game
    bool restart = false; // the game was lost and the player starts again
};

class Mission {
public:
    Mission(Session& session, std::string name);

    // Plays the mission from `room`, the crew coming in the way `spawn`
    // says, until it ends.
    MissionResult run(int room, int spawn);
    // Carries on from a saved game (see save()).
    MissionResult resume(SaveReader& r);
    // The mission as it is now, for a saved game.
    void save(SaveWriter& w) const;
    const std::string& name() const { return name_; }

    // For tests and tools: load a room and let the crew come in without
    // input, for `ticks` ticks.
    void loadRoom(int room);
    void spawnCrew(int how);
    void tickOnce();
    void drawFrame();
    // One tick as the game runs it, without the player.
    void advance(bool draw = true) {
        processEvents();
        tickOnce();
        if (draw) {
            drawFrame();
        }
    }
    const Surface& screen() const { return session_.platform.screen; }

    // A line for each actor, for checking.
    std::string describe() const;
    // For tests and tools: the events this room's code answers, one of them
    // as if it happened, and how things stand afterwards.
    std::vector<Event> roomEvents() const;
    // Walking through each way out of the room, where the crew may now.
    std::vector<Event> exitEvents() const;
    void inject(const Event& e) { post(e.action, e.b1, e.b2, e.b3); }
    bool ended() const { return result_.has_value(); }
    int roomChangingTo() const { return pendingChange_ ? pendingChange_->room : -1; }
    // Goes to the room the code asked for, if it did.
    void applyRoomChange();
    // Takes up a saved mission (see save()) without playing it.
    void restore(SaveReader& r);
    int room() const { return room_; }
    bool lost() const { return lost_; }
    // Nothing is waiting to happen: no events, no one of the crew walking
    // or moving.
    bool idle() const {
        for (const auto& e : events_) {
            if (e.action != kTick) {
                return false;
            }
        }
        for (int i = 0; i < 4; ++i) {
            const Actor& a = actors_[static_cast<std::size_t>(i)];
            if (a.used && a.state != Actor::State::Standing) {
                return false;
            }
        }
        return true;
    }
    // For tools: puts the mouse on a thing of the room (an actor or a
    // place), as when the player points at it.
    void pointAtForTools(int id);
    // For tools: sets a byte of the mission's state block.
    void setStateByte(int offset, std::uint8_t v) {
        if (offset >= 0 && offset < kStateSize) {
            var(offset) = v;
        }
    }
    // For tools: who is in the room and what they do, without where.
    std::string actorsKey() const {
        std::string k;
        for (int i = 0; i < kActors; ++i) {
            const auto& a = actors_[static_cast<std::size_t>(i)];
            if (a.used) {
                // Only what will still lead somewhere: an animation or walk
                // that tells the room when it ends.
                k += std::to_string(i) + (a.notifyFinish && a.state != Actor::State::Standing ? a.animName : std::string()) + ";";
            }
        }
        return k;
    }
    // The mission's state block as it is now.
    std::vector<std::uint8_t> stateBytes() const {
        const std::uint8_t* m = cpu_.memory() + X86::linear(kDataSeg, kState);
        return {m, m + kStateSize};
    }
    const MissionResult* result() const { return result_ ? &*result_ : nullptr; }
    // For tools that play by themselves: no menu when the game is lost (it
    // just ends, lost()), and typed answers are the room's answer number
    // autoAnswer (counted round).
    bool autoplay = false;
    int autoAnswer = 0;

    static constexpr int kActors = 32;

private:
    // The mission's state, kept where the rooms' code finds it.
    static constexpr std::uint16_t kDataSeg = 0x1000;
    static constexpr std::uint16_t kRoomSeg = 0x2000;
    static constexpr std::uint16_t kState = 0xD366;
    static constexpr int kStateSize = 0x129;
    std::uint8_t& var(int offset) { return cpu_.memory()[X86::linear(kDataSeg, static_cast<std::uint16_t>(kState + offset))]; }
    std::uint16_t word(int offset) { return cpu_.read16(kDataSeg, static_cast<std::uint16_t>(kState + offset)); }
    void setWord(int offset, std::uint16_t v) { cpu_.write16(kDataSeg, static_cast<std::uint16_t>(kState + offset), v); }

    // The room file.
    std::uint16_t rdf16(int offset) const;
    std::string rdfText(int offset, int max) const;

    // Rooms.
    void setUpRoom(int room);
    void loadWalkMap(const std::string& name);
    bool blocked(int x, int y) const;
    bool lineClear(int x0, int y0, int x1, int y1) const;
    int nearestNode(int x, int y) const;
    int scaleAt(int y) const;
    int depthOfRow(int y) const;

    // Actors.
    int startAnim(int index, const std::string& name, int x, int y, int scale);
    void loadAnimInto(Actor& a, std::string name, int x, int y, int scale, bool addSprite);
    void removeActor(int index);
    void stopActor(int index); // back to standing, or removed when it has no stand
    void standActor(int index);
    bool walkActor(int index, const std::string& walkName, int sx, int sy, int dx, int dy);
    void startSegment(Actor& a, int sx, int sy, int dx, int dy);
    void setWalkFrame(Actor& a, int x, int y);
    void updateActors();
    void updateAnim(int index);
    void updateWalk(int index);
    std::string crewName(int crew, const std::string& base) const;
    void clearActors();

    // Background animations (.BAN).
    struct Ban {
        std::vector<std::uint8_t> data;
        std::size_t pos = 0;
    };
    void loadBan(const std::string& name);
    void updateBans(bool toScreen);

    // Events.
    void post(std::uint8_t action, std::uint8_t b1 = 0, std::uint8_t b2 = 0, std::uint8_t b3 = 0);
    void handleEvent(const Event& e);
    bool runExact(const Event& e);
    bool runMatching(const Event& e);
    bool hasAction(const Event& e) const;
    void runCode(std::uint16_t offset);
    void defaultResponse(const Event& e);
    void processEvents();

    // Room code calling back.
    void hostCall(X86& cpu);
    std::uint32_t roomFunction(int fn, const std::array<std::uint16_t, 6>& a);

    // Input.
    void handleInput();
    void leftClick();
    void rightClick();
    int objectAt(int x, int y);
    int hotspotAt(int x, int y, bool forAction);
    bool insidePolygon(int listOffset, int x, int y) const;
    bool pointAt(int id, int& x, int& y) const;
    bool walkToObjectFirst();
    void checkTouches();
    int chooseUseTarget();
    void selectAction(int action);
    void targetChosen(int object);
    bool objectUnavailable(int object, int action);
    void showSelectedIcons();
    void clearSelectedIcons();
    void refreshExtraSprites();
    void setCursor(int action, bool highlight);
    void setPointerPicture(const std::string& name);
    Ui ui();
    void updateCursorHighlight();

    // Screens over the room.
    int showText(const std::vector<std::string>& lines, int x, int y, std::uint8_t colour, bool fromRoom);
    int showTextList(std::uint16_t listOffset);
    void showDefault(const std::string& speaker, int textIndex, std::uint8_t colour);
    int actionMenu();
    int inventory(bool forUse);
    void optionsMenu();
    int buttonMenu(const std::string& file, int x, int y, std::uint32_t hidden);
    void saveMenu();
    void loadMenu();
    void quitMenu();
    void textDisplayMenu();
    MissionResult loop();
    void showPicture(const std::string& name);
    void turbolift(int deck);

    void endMission(int a, int b, int c);

    Session& session_;
    std::string name_;
    X86 cpu_;
    std::vector<std::uint8_t> rdf_;
    int room_ = 0;
    Scene scene_;
    std::array<Actor, kActors> actors_{};
    std::vector<const Sprite*> extraSprites_;
    std::vector<std::uint8_t> walkMap_;
    std::vector<std::pair<int, int>> nodes_;
    std::vector<std::uint8_t> nextHop_; // 32 x 32
    std::vector<Ban> bans_;
    int scaleFactor_ = 0;

    std::deque<Event> events_;
    bool lost_ = false;
    std::uint64_t roomTicks_ = 0;
    std::uint64_t frameTicks_ = 0;

    bool warpsEnabled_ = false;
    int lastWarp_ = -1;
    int lastTouched_ = -1;
    bool objectHasWalkTo_ = false;
    int walkToX_ = 0;
    int walkToY_ = 0;
    struct Queued {
        bool used = false;
        Event event;
    };
    std::array<Queued, 32> queued_{};

    int cursorAction_ = 1;
    bool cursorHighlight_ = false;
    std::shared_ptr<const Bitmap> cursor_;
    int randomLookFrame_ = 0;
    bool talkFrame_ = false;
    bool hideCrewFromPointer_ = false;
    Sprite selectedIcon_;
    bool selectedIconShown_ = false;
    Sprite inventoryIcon_;
    bool inventoryIconShown_ = false;

    struct RoomChange {
        int room = 0;
        int spawn = 0;
    };
    std::optional<RoomChange> pendingChange_;
    std::optional<MissionResult> result_;
    bool inRoomCode_ = false;
    std::uint64_t textEndTick_ = 0;
};

// Plays a mission from `room`, and carries on from saved games the player
// loads along the way, until a mission ends or the player quits.
MissionResult playAway(Session& session, const std::string& mission, int room, int spawn);

} // namespace helmsman::away
