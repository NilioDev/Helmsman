// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saved games of the second game, in Helmsman's own save files (see
// docs/formats/savegame.md): the scripts keep all they know in the data
// segment, so a save is that segment and, on an away mission, what Helmsman
// itself keeps of the room. The episodes' checkpoints (the space script's
// "save here", which a lost mission goes back to) are saves of their own.
#include "game_impl.hpp"

#include "helmsman/engine/savegame.hpp"

#include <ctime>

namespace helmsman::sequel::detail {

namespace {

constexpr const char* kGame = "judgment";
constexpr std::uint8_t kFormat = 3; // 1: without the ships in space, 2: without the music
constexpr std::uint8_t kOnBridge = 0;
constexpr std::uint8_t kOnMission = 1;
constexpr std::size_t kSegment = 0x10000;
constexpr std::uint32_t kMaxPath = 64;
constexpr std::uint32_t kMaxQueue = 16;

std::string timeNow() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%d %b %H:%M", &local);
    return buf;
}

std::string checkpointGame(const std::string& name) { return std::string(kGame) + "-checkpoint-" + lowerName(name); }

} // namespace

std::vector<std::uint8_t> Game::savePayload() {
    SaveWriter w;
    w.u8(kFormat);
    w.u8(away_ ? kOnMission : kOnBridge);
    const std::uint8_t* ds = m_.cpu.memory() + X86::linear(Machine::kData, 0);
    w.bytes(std::span<const std::uint8_t>(ds, kSegment));
    setupSpace();
    space_->save(w);
    saveMusic(w);
    if (away_) {
        writeAway(w);
    }
    return std::move(w.data);
}

void Game::writeAway(SaveWriter& w) const {
    w.s32(roomNumber_);
    w.text(backdropName_);
    w.text(depthsName_);
    w.text(walkMapName_);
    w.s32(fade_);
    w.s32(fadeTarget_);
    w.u8(fadeColour_.r);
    w.u8(fadeColour_.g);
    w.u8(fadeColour_.b);
    w.u32(roomTicks_);
    w.s32(area_);
    w.s32(exitArmed_);
    w.u8(busy_ ? 1 : 0);
    w.u32(static_cast<std::uint32_t>(queue_.size()));
    for (const auto& e : queue_) {
        for (const auto b : e) {
            w.u8(b);
        }
    }
    for (const Body& b : bodies_) {
        w.u8(b.used ? 1 : 0);
        if (!b.used) {
            continue;
        }
        w.text(b.anim);
        w.u32(static_cast<std::uint32_t>(b.frame));
        w.u32(static_cast<std::uint32_t>(b.nextAt > awayTicks_ ? b.nextAt - awayTicks_ : 0));
        w.s32(b.x);
        w.s32(b.y);
        w.s32(b.scale);
        w.u8(b.walking ? 1 : 0);
        w.u32(static_cast<std::uint32_t>(b.path.size()));
        for (const auto& [x, y] : b.path) {
            w.s32(x);
            w.s32(y);
        }
        w.s32(b.fx);
        w.s32(b.fy);
        w.s32(b.vx);
        w.s32(b.vy);
        w.s32(b.steps);
        w.s32(b.walkFrame);
        w.u8(static_cast<std::uint8_t>(b.dir));
        w.s32(b.code);
        w.u8(b.held ? 1 : 0);
        w.u8(static_cast<std::uint8_t>(b.sprite.mode));
        w.s32(b.sprite.order);
    }
}

void Game::readAway(SaveReader& r) {
    const int room = r.s32();
    const std::string backdrop = r.text();
    const std::string depths = r.text();
    const std::string walkMap = r.text();
    loadRoom(room, true, true);
    if (backdrop != backdropName_) {
        if (const auto bgd = file(backdrop + ".BGD"); bgd && bgd->size() >= 768 + 64000) {
            std::copy_n(bgd->begin() + 768, 64000, scene_.backdrop.data());
            backdropName_ = backdrop;
        }
    }
    if (depths != depthsName_) {
        loadDepths(depths);
    }
    if (walkMap != walkMapName_) {
        loadWalkMap(walkMap);
    }
    fade_ = std::clamp(r.s32(), 0, 100);
    fadeTarget_ = std::clamp(r.s32(), 0, 100);
    fadeColour_.r = static_cast<std::uint8_t>(r.u8() & 0x3F);
    fadeColour_.g = static_cast<std::uint8_t>(r.u8() & 0x3F);
    fadeColour_.b = static_cast<std::uint8_t>(r.u8() & 0x3F);
    roomTicks_ = r.u32();
    area_ = r.s32();
    exitArmed_ = r.s32();
    busy_ = r.u8() != 0;
    const auto queued = r.u32();
    if (queued > kMaxQueue) {
        throw FormatError("a saved game with too many events waiting");
    }
    for (std::uint32_t i = 0; i < queued; ++i) {
        std::array<std::uint8_t, 4> e{};
        for (auto& b : e) {
            b = r.u8();
        }
        queue_.push_back(e);
    }
    for (int i = 0; i < 32; ++i) {
        Body& b = bodies_[i];
        b = Body{};
        if (r.u8() == 0) {
            continue;
        }
        b.used = true;
        b.anim = r.text();
        b.frames = &anim(b.anim);
        b.frame = r.u32();
        if (!b.frames->empty()) {
            b.frame = std::min(b.frame, b.frames->size() - 1);
        } else {
            b.frame = 0;
        }
        b.nextAt = awayTicks_ + std::min<std::uint32_t>(r.u32(), 18 * 60);
        b.x = r.s32();
        b.y = r.s32();
        b.scale = std::clamp(r.s32(), 1, 1024);
        b.walking = r.u8() != 0;
        const auto legs = r.u32();
        if (legs > kMaxPath) {
            throw FormatError("a saved game with too long a walk");
        }
        for (std::uint32_t k = 0; k < legs; ++k) {
            const int x = r.s32();
            const int y = r.s32();
            b.path.emplace_back(x, y);
        }
        b.fx = r.s32();
        b.fy = r.s32();
        b.vx = r.s32();
        b.vy = r.s32();
        b.steps = r.s32();
        b.walkFrame = r.s32();
        b.dir = static_cast<char>(r.u8());
        b.code = r.s32();
        b.held = r.u8() != 0;
        const auto mode = r.u8();
        b.sprite.mode = mode <= static_cast<std::uint8_t>(away::SpriteMode::Text) ? static_cast<away::SpriteMode>(mode)
                                                                                   : away::SpriteMode::Normal;
        b.sprite.order = r.s32();
        if (b.walking) {
            setWalkPicture(b, i);
        } else {
            placeSprite(b);
        }
    }
}

// Puts the game where a save was made. The bridge loop or the away
// mission it was made on picks up from there.
void Game::restorePayload(std::span<const std::uint8_t> payload) {
    SaveReader r(payload);
    const auto format = r.u8();
    if (format < 1 || format > kFormat) {
        throw FormatError("a saved game of another version");
    }
    const auto where = r.u8();
    if (where != kOnBridge && where != kOnMission) {
        throw FormatError("a damaged saved game");
    }
    const auto ds = r.blob(kSegment);
    if (ds.size() != kSegment) {
        throw FormatError("a damaged saved game");
    }
    std::copy(ds.begin(), ds.end(), m_.cpu.memory() + X86::linear(Machine::kData, 0));
    startEpisode(false);
    // The data segment already holds the fight's own state.
    resetSpace(false);
    if (format >= 2) {
        space_->restore(r);
    }
    if (format >= 3) {
        restoreMusic(r);
    }
    applyView();
    if (where == kOnMission) {
        resumeAway_ = std::vector<std::uint8_t>(payload.begin() + static_cast<std::ptrdiff_t>(r.pos()), payload.end());
        awayPending_ = true;
    } else {
        resumeAway_ = std::nullopt;
    }
}

void Game::writeCheckpoint(const std::string& name) {
    m_.set16(kRestoreCode, 0);
    const auto payload = savePayload();
    writeSave(p_.saveFolder, checkpointGame(name), 0, name, payload);
}

bool Game::anySaves() const { return !listSaves(p_.saveFolder, kGame).empty(); }

void Game::saveMenu() {
    const auto saves = listSaves(p_.saveFolder, kGame);
    std::vector<std::string> lines;
    const bool room = static_cast<int>(saves.size()) < kMaxSaves;
    if (room) {
        lines.push_back("#GENER023#Save new game.");
    }
    for (const auto& s : saves) {
        lines.push_back("Replace: " + s.description);
    }
    const int choice = menuBox("Save Game", lines);
    if (choice < 0) {
        return;
    }
    const int number = room && choice == 0 ? freeSaveNumber(p_.saveFolder, kGame)
                                           : saves[static_cast<std::size_t>(choice - (room ? 1 : 0))].number;
    char description[64];
    std::snprintf(description, sizeof description, "Episode %d, %s", m_.s16(ds::kEpisode) + 1, timeNow().c_str());
    if (number < 0 || !writeSave(p_.saveFolder, kGame, number, description, savePayload())) {
        menuBox("Save Game", {"The game could not be saved."});
    }
}

// Asks which save to load and goes there (by throwing RestoreGame); false
// when the player backs out.
bool Game::loadMenu() {
    const auto saves = listSaves(p_.saveFolder, kGame);
    if (saves.empty()) {
        menuBox("Load Game", {"There are no saved games."});
        return false;
    }
    std::vector<std::string> lines;
    for (const auto& s : saves) {
        lines.push_back(s.description);
    }
    const int choice = menuBox("Load Game", lines);
    if (choice < 0) {
        return false;
    }
    auto data = readSave(p_.saveFolder, kGame, saves[static_cast<std::size_t>(choice)].number);
    if (!data) {
        menuBox("Load Game", {"Unable to load game."});
        return false;
    }
    throw RestoreGame{std::move(*data), -1};
}

// The options (the insignia of Kirk's menu, or the away mission's): the
// original's panel of buttons. True when the game is to end.
bool Game::optionsMenu() {
    HoldSpace hold(*this);
    std::vector<MenuButton> buttons;
    for (const auto& b : menu("MNUOPTN")) {
        // The text display button stays out: Helmsman always shows the text.
        // Of each on/off pair only the one that changes something shows.
        if (b.id == 7 || (b.id == 2 && soundOn_) || (b.id == 3 && !soundOn_) || (b.id == 4 && musicOn_) ||
            (b.id == 5 && !musicOn_)) {
            continue;
        }
        buttons.push_back(b);
    }
    if (buttons.empty()) {
        return false;
    }
    // Close the gaps the left-out buttons leave.
    int top = 1000;
    for (const auto& b : buttons) {
        top = std::min(top, b.y);
    }
    switch (chooseButtons(buttons, (320 - 64) / 2, 40 - top)) {
    case 0:
        saveMenu();
        break;
    case 1:
        loadMenu();
        break;
    case 2:
    case 3:
        soundOn_ = !soundOn_;
        if (!soundOn_) {
            p_.stopSound(SoundChannel::Effect);
            p_.stopSound(SoundChannel::Ambient);
        }
        break;
    case 4:
    case 5:
        setMusicOn(!musicOn_);
        break;
    case 6:
        if (menuBox("Quit", {"#GENER018#Quit the game.", "Cancel."}) == 0) {
            p_.quit = true;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// A lost mission: load, start over or quit (room command 0x14).
void Game::gameOverMenu() {
    if (explore_) {
        exploreJump("lost");
    }
    const bool any = anySaves();
    std::vector<std::string> lines;
    if (any) {
        lines.push_back("#GENER006#Load a previously saved game.");
    }
    lines.push_back("#GENER020#Restart the game.");
    lines.push_back("#GENER018#Quit the game.");
    while (!p_.quit) {
        int choice = menuBox("Game Over", lines);
        if (choice < 0) {
            continue;
        }
        if (!any) {
            ++choice;
        }
        switch (choice) {
        case 0:
            loadMenu();
            break;
        case 1:
            throw RestartGame{};
        default:
            p_.quit = true;
            throw LeaveMission{0};
        }
    }
}

std::optional<std::vector<std::uint8_t>> Game::readCheckpoint(const std::string& name) {
    return readSave(p_.saveFolder, checkpointGame(name), 0);
}

} // namespace helmsman::sequel::detail
