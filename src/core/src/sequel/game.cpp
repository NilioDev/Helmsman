// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/sequel/game.hpp"

#include "helmsman/away/assets.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/data/sound_bank.hpp"
#include "helmsman/engine/pad_buttons.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/sequel/machine.hpp"
#include "helmsman/sequel/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <random>

namespace helmsman::sequel {

namespace {

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

std::string upperName(std::string_view s) {
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
    void startEpisode();

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
    void sayLine(const Line& line) { say(line.who, {line.text}, 0); }
    void playVoice(const std::string& tag);

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

std::optional<std::vector<std::uint8_t>> Game::file(std::string_view name) const {
    const std::string n = upperName(name);
    if (episode_) {
        if (auto bytes = episode_->load(n)) {
            return bytes;
        }
    }
    return assets_.data().load(n);
}

std::shared_ptr<const Bitmap> Game::picture(std::string_view name) {
    const std::string n = upperName(name);
    const auto it = pictures_.find(n);
    if (it != pictures_.end()) {
        return it->second;
    }
    std::shared_ptr<const Bitmap> pic;
    if (const auto bytes = file(n + ".BMP")) {
        pic = std::make_shared<const Bitmap>(Bitmap::fromBytes(*bytes));
    }
    pictures_[n] = pic;
    return pic;
}

const std::vector<away::AnimFrame>& Game::anim(std::string_view name) {
    const std::string n = upperName(name);
    const auto it = anims_.find(n);
    if (it != anims_.end()) {
        return it->second;
    }
    std::vector<away::AnimFrame> frames;
    if (const auto bytes = file(n + ".ANM")) {
        frames = away::parseAnim(*bytes);
    }
    return anims_[n] = std::move(frames);
}

std::vector<MenuButton> Game::menu(std::string_view name) {
    std::vector<MenuButton> out;
    const auto bytes = file(std::string(name) + ".MNU");
    if (!bytes) {
        return out;
    }
    Reader r(*bytes);
    while (r.remaining() >= 16) {
        MenuButton b;
        const auto n = r.bytes(10);
        for (const auto c : n) {
            if (c == 0 || c == ' ') {
                break;
            }
            b.picture.push_back(static_cast<char>(c));
        }
        b.x = r.s16();
        b.y = r.s16();
        b.id = r.s16();
        out.push_back(b);
    }
    return out;
}

void Game::startEpisode() {
    episodeName_ = m_.text(ds::kEpisodeName, 16);
    const std::string base = upperName(episodeName_);
    episode_.emplace(Archive::open(assets_.source(), base));
    try {
        voices_.emplace(assets_.source(), base + ".VCC");
    } catch (const FormatError&) {
        voices_.reset();
    }
    pictures_.clear();
    anims_.clear();
    m_.loadSpaceScript(episode_->need(base + "S.RDF"));
}

void Game::setSprite(Sprite& s, std::string_view name) {
    s.name = upperName(name);
    s.frames = anim(s.name);
    s.frame = 0;
    s.nextAt = static_cast<double>(p_.millis);
    s.shown = !s.frames.empty();
}

void Game::loadBridge() {
    const auto bytes = assets_.data().need("BRIDGE.BGD");
    if (bytes.size() < 768 + 64000) {
        throw FormatError("bridge picture too short");
    }
    palette_ = Palette::fromBytes(std::span<const std::uint8_t>(bytes).first(768));
    background_ = Surface(320, 200);
    std::copy_n(bytes.begin() + 768, 64000, background_.data());
    shade_ = shadeTable(palette_, 45);
    for (const auto& st : kStations) {
        setSprite(crew_[st.actor], st.anim);
    }
}

void Game::tickSprites() {
    const double now = static_cast<double>(p_.millis);
    auto step = [&](Sprite& s) {
        if (!s.shown || s.frames.empty()) {
            return;
        }
        if (s.still) {
            s.frame = 0;
            s.nextAt = now;
            return;
        }
        while (now >= s.nextAt) {
            const auto& f = s.frames[s.frame];
            const int pick = static_cast<int>(rng_() % 4);
            std::size_t next = f.next[pick];
            if (next >= s.frames.size()) {
                next = 0;
            }
            s.frame = next;
            s.nextAt += std::max(1, s.frames[s.frame].ticks) * kTickMillis;
        }
    };
    for (auto& s : crew_) {
        step(s);
    }
    for (auto& s : talk_) {
        step(s);
    }
    step(viewscreen_);
}

void Game::drawSprite(const Sprite& s) {
    if (!s.shown || s.frames.empty()) {
        return;
    }
    const auto& f = s.frames[std::min(s.frame, s.frames.size() - 1)];
    if (const auto pic = picture(f.picture)) {
        pic->draw(p_.screen, s.x + f.x, s.y + f.y);
    }
}

void Game::drawBridge() {
    std::copy_n(background_.data(), background_.size(), p_.screen.data());
    drawSprite(viewscreen_);
    for (int i = 0; i < kCrew; ++i) {
        drawSprite(talk_[i].shown ? talk_[i] : crew_[i]);
    }
    p_.palette = palette_;
}

void Game::present() {
    tickSprites();
    p_.waitFrame();
    pumpTicks();
}

void Game::pumpTicks() {
    const double now = static_cast<double>(p_.millis);
    if (nextTick_ == 0) {
        nextTick_ = now;
    }
    while (now >= nextTick_) {
        nextTick_ += kTickMillis;
        ++ticksDue_;
    }
}

void Game::waitTicks(int ticks) {
    const double end = static_cast<double>(p_.millis) + ticks * kTickMillis;
    while (!p_.quit && static_cast<double>(p_.millis) < end) {
        drawBridge();
        present();
    }
}

int Game::crewAt(int x, int y) {
    for (int i = 0; i < kCrew; ++i) {
        const Sprite& s = crew_[i];
        if (!s.shown || s.frames.empty()) {
            continue;
        }
        const auto& f = s.frames[s.frame];
        const auto pic = picture(f.picture);
        if (!pic) {
            continue;
        }
        const int left = f.x - pic->handleX;
        const int top = f.y - pic->handleY;
        if (x >= left && y >= top && x < left + pic->width() && y < top + pic->height() &&
            pic->pixels.at(x - left, y - top) != 0) {
            return i;
        }
    }
    return -1;
}

// The star map: the player points at a star system and picks it. Returns
// its number, or -1 when the player changed their mind.
int Game::starMap() {
    const auto bytes = assets_.data().need("STARMAP.BGD");
    if (bytes.size() < 768 + 64000) {
        throw FormatError("star map too short");
    }
    const Palette pal = Palette::fromBytes(std::span<const std::uint8_t>(bytes).first(768));
    const auto cross = picture("CROSS");
    const auto circle = picture("CIRCLE");
    p_.takeClicks();
    p_.keys.clear();
    int chosen = -2;
    int keyPick = m_.s16(ds::kSystem);
    if (keyPick < 0 || keyPick >= kSystemCount) {
        keyPick = 0;
    }
    bool byKeys = false;
    int lastX = p_.mouseX;
    int lastY = p_.mouseY;
    while (!p_.quit && chosen == -2) {
        std::copy_n(bytes.begin() + 768, 64000, p_.screen.data());
        p_.palette = pal;
        if (p_.mouseX != lastX || p_.mouseY != lastY) {
            byKeys = false;
            lastX = p_.mouseX;
            lastY = p_.mouseY;
        }
        // The nearest system to the pointer, as the original measures it.
        int nearest = keyPick;
        if (!byKeys) {
            int best = 0x7FFF;
            for (int i = 0; i < kSystemCount; ++i) {
                const int sx = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i));
                const int sy = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i + 2));
                const int d = std::abs(sx - p_.mouseX) + std::abs(sy - p_.mouseY);
                if (d < best) {
                    best = d;
                    nearest = i;
                }
            }
        }
        p_.pointerTargets.clear();
        for (int i = 0; i < kSystemCount; ++i) {
            const int sx = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i));
            const int sy = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i + 2));
            p_.pointerTargets.push_back(ClipRect{sx - 3, sy - 3, sx + 3, sy + 3});
        }
        const int nx = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * nearest));
        const int ny = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * nearest + 2));
        if (circle) {
            circle->draw(p_.screen, nx - circle->width() / 2 + circle->handleX, ny - circle->height() / 2 + circle->handleY);
        }
        if (cross && !byKeys) {
            cross->draw(p_.screen, p_.mouseX - cross->width() / 2 + cross->handleX,
                        p_.mouseY - cross->height() / 2 + cross->handleY);
        }
        p_.pointerShown = false;
        present();
        const auto clicks = p_.takeClicks();
        if ((clicks & 2) != 0) {
            chosen = -1;
        } else if ((clicks & 1) != 0) {
            chosen = nearest;
        }
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEscape) {
                chosen = -1;
            } else if (k.scancode == scancode::kEnter || k.scancode == scancode::kSpace) {
                chosen = nearest;
            } else if (k.scancode == scancode::kLeft || k.scancode == scancode::kRight || k.scancode == scancode::kUp ||
                       k.scancode == scancode::kDown) {
                // The system that lies nearest in that direction.
                const int dx = k.scancode == scancode::kLeft ? -1 : k.scancode == scancode::kRight ? 1 : 0;
                const int dy = k.scancode == scancode::kUp ? -1 : k.scancode == scancode::kDown ? 1 : 0;
                int best = 0x7FFF;
                int pick = nearest;
                for (int i = 0; i < kSystemCount; ++i) {
                    const int ex = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i)) - nx;
                    const int ey = m_.s16(static_cast<std::uint16_t>(kSystems + 4 * i + 2)) - ny;
                    const int along = ex * dx + ey * dy;
                    if (along <= 0) {
                        continue;
                    }
                    const int score = along + 2 * std::abs(ex * dy - ey * dx);
                    if (score < best) {
                        best = score;
                        pick = i;
                    }
                }
                keyPick = pick;
                byKeys = true;
            }
        }
        if (!byKeys) {
            keyPick = nearest;
        }
        p_.keys.clear();
    }
    p_.pointerShown = true;
    return chosen < 0 ? -1 : chosen;
}

// Shows a crew member's menu where the original puts it and waits for a
// choice: the button's number, or -1.
int Game::chooseFromMenu(int who) {
    const auto at = static_cast<std::uint16_t>(kMenuTable + who * kMenuEntry);
    const std::string name = m_.text(at, 10);
    if (name.empty()) {
        return -1;
    }
    const int mx = m_.s16(static_cast<std::uint16_t>(at + 10));
    const int my = m_.s16(static_cast<std::uint16_t>(at + 12));
    const auto buttons = menu(name);
    if (buttons.empty()) {
        return -1;
    }
    p_.takeClicks();
    p_.keys.clear();
    while (!p_.quit) {
        drawBridge();
        p_.pointerTargets.clear();
        int hover = -1;
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto& b = buttons[i];
            const auto pic = picture(b.picture);
            if (!pic) {
                continue;
            }
            const int x = mx + b.x;
            const int y = my + b.y;
            pic->copyTo(p_.screen, x, y);
            p_.pointerTargets.push_back(ClipRect{x, y, x + pic->width() - 1, y + pic->height() - 1});
            if (p_.mouseX >= x && p_.mouseY >= y && p_.mouseX < x + pic->width() && p_.mouseY < y + pic->height()) {
                hover = static_cast<int>(i);
            }
        }
        if (hover >= 0) {
            const auto pic = picture(buttons[static_cast<std::size_t>(hover)].picture);
            const int x = mx + buttons[static_cast<std::size_t>(hover)].x;
            const int y = my + buttons[static_cast<std::size_t>(hover)].y;
            for (int i = 0; i < pic->width(); ++i) {
                p_.screen.set(x + i, y, 15);
                p_.screen.set(x + i, y + pic->height() - 1, 15);
            }
            for (int j = 0; j < pic->height(); ++j) {
                p_.screen.set(x, y + j, 15);
                p_.screen.set(x + pic->width() - 1, y + j, 15);
            }
        }
        present();
        const auto clicks = p_.takeClicks();
        bool cancel = (clicks & 2) != 0;
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEscape) {
                cancel = true;
            }
        }
        p_.keys.clear();
        if (cancel) {
            return -1;
        }
        if ((clicks & 1) != 0) {
            return hover >= 0 ? buttons[static_cast<std::size_t>(hover)].id : -1;
        }
    }
    return -1;
}

void Game::playVoice(const std::string& tag) {
    if (tag.empty()) {
        return;
    }
    std::optional<PlainSound> sound;
    if (voices_ && voices_->has(tag)) {
        try {
            sound = voices_->sound(tag);
        } catch (const FormatError&) {
        }
    }
    if (!sound) {
        sound = assets_.sound(tag);
    }
    if (sound) {
        p_.queueSound(SoundChannel::Voice, sound->rate, std::move(sound->samples), true);
    }
}

// Shows a line, or (with more than one text) a choice, and waits for the
// player. Returns the choice made (0 the first).
int Game::say(int who, const std::vector<std::string>& texts, std::uint16_t seg) {
    if (texts.empty()) {
        return 0;
    }
    // Who says it: a person, or a place in the script with a name.
    int set = 0;
    int bx = 20;
    int by = 20;
    std::string name;
    if (who >= 0 && who < 0x20) {
        const auto rec = static_cast<std::uint16_t>(ds::kActors + who * ds::kActorSize);
        set = m_.u8(static_cast<std::uint16_t>(rec + actor::kTextSet));
        bx = m_.s16(static_cast<std::uint16_t>(rec + actor::kTextX));
        by = m_.s16(static_cast<std::uint16_t>(rec + actor::kTextY));
        bx = bx == 0 ? 20 : bx;
        by = by == 0 ? 20 : by;
        name = m_.text(static_cast<std::uint16_t>(rec + actor::kName), 24);
    } else if (who >= 0x20 && seg != 0) {
        bx = static_cast<std::int16_t>(m_.cpu.read16(seg, static_cast<std::uint16_t>(who)));
        by = static_cast<std::int16_t>(m_.cpu.read16(seg, static_cast<std::uint16_t>(who + 2)));
        set = m_.cpu.read8(seg, static_cast<std::uint16_t>(who + 4));
        name = m_.scriptText(seg, static_cast<std::uint16_t>(who + 6), 24);
    }
    const bool choice = texts.size() > 1;
    constexpr std::size_t kColumns = 30;
    constexpr int kRowsPerPage = 8;

    // The rows of each text (one page set for a line, one block per
    // choice).
    std::vector<std::vector<std::string>> blocks;
    std::string voice;
    for (std::string t : texts) {
        const auto tag = takeSoundTag(t);
        if (!choice) {
            voice = tag;
        }
        blocks.push_back(wrapText(t, kColumns));
    }
    if (!choice) {
        // Talking animation while the line is said.
        if (pendingTalker_ >= 0 && pendingTalker_ < kCrew && !pendingTalkAnim_.empty()) {
            setSprite(talk_[pendingTalker_], pendingTalkAnim_);
        } else if (pendingTalker_ == kViewscreenSpeaker && viewscreenTalks_) {
            viewscreen_.still = false;
        }
        playVoice(voice);
    }

    std::vector<std::string> rows;
    std::vector<int> rowOwner;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        for (const auto& r : blocks[i]) {
            rows.push_back(r);
            rowOwner.push_back(static_cast<int>(i));
        }
    }
    std::size_t page = 0;
    const std::size_t pages = choice ? 1 : (rows.size() + kRowsPerPage - 1) / kRowsPerPage;
    int picked = -1;
    p_.takeClicks();
    p_.keys.clear();
    int keyChoice = 0;
    while (!p_.quit) {
        drawBridge();
        const std::size_t first = choice ? 0 : page * kRowsPerPage;
        const std::size_t count = choice ? rows.size() : std::min<std::size_t>(kRowsPerPage, rows.size() - first);
        std::size_t widest = name.size();
        for (std::size_t i = 0; i < count; ++i) {
            widest = std::max(widest, rows[first + i].size());
        }
        const int w = static_cast<int>(widest) * SequelFont::kWidth + 12;
        const int h = static_cast<int>(count + (name.empty() ? 0 : 1)) * SequelFont::kHeight + 10;
        const int x = std::clamp(bx, 2, 318 - w);
        const int y = std::clamp(by, 2, 198 - h);
        // A see-through dark box with a frame in the speaker's colour.
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                p_.screen.set(xx, yy, shade_[p_.screen.at(xx, yy)]);
            }
        }
        const std::uint8_t frame = m_.u8(static_cast<std::uint16_t>(kTextColours + std::clamp(set, 0, 9) * 8 + 1));
        for (int xx = x; xx < x + w; ++xx) {
            p_.screen.set(xx, y, frame);
            p_.screen.set(xx, y + h - 1, frame);
        }
        for (int yy = y; yy < y + h; ++yy) {
            p_.screen.set(x, yy, frame);
            p_.screen.set(x + w - 1, yy, frame);
        }
        int ty = y + 5;
        if (!name.empty()) {
            font_.drawText(p_.screen, x + 6, ty, name, set);
            ty += SequelFont::kHeight;
        }
        p_.pointerTargets.clear();
        int hover = -1;
        for (std::size_t i = 0; i < count; ++i) {
            const int owner = rowOwner[first + i];
            const int rowY = ty + static_cast<int>(i) * SequelFont::kHeight;
            bool lit = false;
            if (choice) {
                if (p_.mouseX >= x && p_.mouseX < x + w && p_.mouseY >= rowY && p_.mouseY < rowY + SequelFont::kHeight) {
                    hover = owner;
                }
                lit = owner == (hover >= 0 ? hover : keyChoice);
                p_.pointerTargets.push_back(ClipRect{x + 4, rowY, x + w - 5, rowY + SequelFont::kHeight - 1});
            }
            font_.drawText(p_.screen, x + 6, rowY, rows[first + i], choice ? (lit ? 9 : 0) : set);
        }
        if (!choice) {
            p_.pointerTargets.push_back(ClipRect{x, y, x + w - 1, y + h - 1});
        }
        present();

        const auto clicks = p_.takeClicks();
        bool next = (clicks & 1) != 0;
        for (const auto& k : p_.keys) {
            if (k.scancode == scancode::kEnter || k.scancode == scancode::kSpace || k.scancode == scancode::kEscape) {
                next = true;
                if (choice && hover < 0) {
                    hover = keyChoice;
                }
            } else if (choice && k.scancode == scancode::kUp) {
                keyChoice = std::max(0, keyChoice - 1);
            } else if (choice && k.scancode == scancode::kDown) {
                keyChoice = std::min(static_cast<int>(blocks.size()) - 1, keyChoice + 1);
            } else if (choice && k.text >= '1' && k.text <= '9' && k.text - '1' < static_cast<int>(blocks.size())) {
                hover = k.text - '1';
                next = true;
            }
        }
        p_.keys.clear();
        if (!next) {
            continue;
        }
        if (choice) {
            if (hover >= 0) {
                picked = hover;
                break;
            }
            continue;
        }
        if (++page >= pages) {
            break;
        }
    }
    if (!choice) {
        p_.stopSound(SoundChannel::Voice);
        for (auto& t : talk_) {
            t.shown = false;
        }
        viewscreen_.still = viewscreenTalks_;
        pendingTalker_ = -100;
        pendingTalkAnim_.clear();
    }
    p_.pointerTargets.clear();
    return std::max(0, picked);
}

// A line or a choice as the scripts lay it out: who says it, then the
// offsets of the texts, ending with 0.
int Game::sayStruct(std::uint16_t seg, std::uint16_t at) {
    const auto who = static_cast<std::int16_t>(m_.cpu.read16(seg, at));
    std::vector<std::string> texts;
    for (int i = 1; i < 16; ++i) {
        const auto off = m_.cpu.read16(seg, static_cast<std::uint16_t>(at + 2 * i));
        if (off == 0) {
            break;
        }
        texts.push_back(m_.scriptText(seg, off));
    }
    if (pendingTalker_ == kChoiceSpeaker) {
        // A choice is always the captain's.
        return say(0, texts.size() > 1 ? texts : std::vector<std::string>{texts.empty() ? "" : texts[0]}, seg);
    }
    return say(who, texts, seg);
}

Machine::Flow Game::spaceCommand(ScriptCall& c, std::uint32_t& result) {
    const auto seg = Machine::kSpace;
    result = 0;
    switch (c.function) {
    case cmd::kTitles:
        m_.set16(ds::kResume, static_cast<std::uint16_t>(m_.u16(ds::kResume) + 3));
        m_.set16(place::kPauseTicks, 1);
        return Machine::Flow::Yield;
    case cmd::kEndTitles:
        playScene(p_, assets_, "ENDGAME", &*episode_);
        leaveBridge_ = true;
        p_.quit = true;
        return Machine::Flow::Yield;
    case cmd::kViewscreen: {
        auto name = m_.scriptText(seg, c.arg(0), 10);
        // "tlk" before a name: someone who talks on the viewscreen, whose
        // mouth only moves while a line is said.
        const bool talks = name.size() > 3 && upperName(name.substr(0, 3)) == "TLK";
        if (talks) {
            name.erase(0, 3);
        }
        setSprite(viewscreen_, name);
        viewscreen_.still = talks;
        viewscreenTalks_ = talks;
        viewscreen_.x = kViewscreenX;
        viewscreen_.y = kViewscreenY;
        return Machine::Flow::Go;
    }
    case cmd::kViewscreenOn:
        viewscreen_.shown = !viewscreen_.frames.empty();
        return Machine::Flow::Go;
    case cmd::kViewscreenOff:
        viewscreen_.shown = false;
        return Machine::Flow::Go;
    case cmd::kSetDestination:
        m_.set16(ds::kDestination, c.arg(0));
        return Machine::Flow::Go;
    case cmd::kEnterOrbit:
        m_.set16(place::kInOrbit, 1);
        return Machine::Flow::Go;
    case cmd::kLeaveOrbit:
        m_.set16(place::kInOrbit, 0);
        return Machine::Flow::Go;
    case cmd::kSay:
        result = static_cast<std::uint32_t>(sayStruct(seg, c.arg(0)));
        return Machine::Flow::Go;
    case cmd::kPause:
        m_.set16(place::kPauseTicks, c.arg(0) == 0 ? 1 : c.arg(0));
        return Machine::Flow::Yield;
    case cmd::kEventLoop:
        m_.set16(place::kPauseTicks, 0);
        return Machine::Flow::Yield;
    case cmd::kAway:
        m_.set16(ds::kRoom, c.arg(1));
        m_.set16(ds::kSpawn, c.arg(2));
        m_.set16(place::kPauseTicks, 1);
        say(-1, {"(Away missions are not playable yet.)"}, 0);
        return Machine::Flow::Yield;
    case cmd::kEpisodeOver:
        leaveBridge_ = true;
        return Machine::Flow::Yield;
    case cmd::kRandom: {
        const int lo = c.sarg(0);
        const int hi = c.sarg(1);
        result = static_cast<std::uint32_t>(hi < lo ? lo : lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1)));
        return Machine::Flow::Go;
    }
    case cmd::kShowCrew: {
        const auto rec = static_cast<std::uint16_t>(ds::kActors + c.arg(0) * ds::kActorSize);
        m_.set8(static_cast<std::uint16_t>(rec + actor::kTeam), 0xBD);
        m_.set8(rec, static_cast<std::uint8_t>(m_.u8(rec) | 8));
        return Machine::Flow::Go;
    }
    case cmd::kHideCrew: {
        const auto rec = static_cast<std::uint16_t>(ds::kActors + c.arg(0) * ds::kActorSize);
        m_.set8(static_cast<std::uint16_t>(rec + actor::kTeam), 0);
        m_.set8(rec, static_cast<std::uint8_t>(m_.u8(rec) & ~8));
        return Machine::Flow::Go;
    }
    case cmd::kCrewFlags: {
        const auto rec = static_cast<std::uint16_t>(ds::kActors + c.arg(0) * ds::kActorSize);
        const auto a = static_cast<std::uint8_t>(c.arg(1));
        const auto b = static_cast<std::uint8_t>(c.arg(2));
        const auto old = m_.u8(static_cast<std::uint16_t>(rec + actor::kTeam));
        const auto now = static_cast<std::uint8_t>((static_cast<std::uint8_t>(~(a ^ b)) & old) ^ a);
        m_.set8(static_cast<std::uint16_t>(rec + actor::kTeam), now);
        m_.set8(rec, static_cast<std::uint8_t>((m_.u8(rec) & ~8) | (now & 8)));
        return Machine::Flow::Go;
    }
    case cmd::kScene: {
        const auto name = m_.scriptText(seg, c.arg(0), 10);
        playScene(p_, assets_, name, &*episode_);
        return Machine::Flow::Go;
    }
    case cmd::kWait:
        waitTicks(c.sarg(0));
        return Machine::Flow::Go;
    case cmd::kCheckpoint:
        m_.set16(place::kPauseTicks, 1);
        return Machine::Flow::Yield;
    case cmd::kRestored:
        return Machine::Flow::Go;
    case cmd::kSpeakerAnim: {
        const int who = c.sarg(0);
        pendingTalker_ = who;
        pendingTalkAnim_ = m_.scriptText(seg, c.arg(1), 10);
        if (pendingTalkAnim_ == "NOANIM") {
            pendingTalkAnim_.clear();
        }
        result = static_cast<std::uint32_t>(static_cast<std::uint16_t>(who));
        return Machine::Flow::Go;
    }
    case cmd::kMusic:
    case cmd::kSong:
    case cmd::kOpenBank:
    case cmd::kCloseBanks:
        return Machine::Flow::Go;
    default:
        std::fprintf(stderr, "space command %02x (%04x %04x %04x) not done yet\n", c.function, c.arg(0), c.arg(1),
                     c.arg(2));
        return Machine::Flow::Go;
    }
}

void Game::resumeSpace(std::uint16_t at) {
    m_.runSpace(at, [this](ScriptCall& c, std::uint32_t& result) { return spaceCommand(c, result); });
}

// Runs an event loop's handler, if the script has one.
bool Game::handle(int offset) {
    const auto table = m_.u16(ds::kResume);
    const auto at = m_.spaceWord(static_cast<std::uint16_t>(table + offset));
    if (at == 0) {
        return false;
    }
    resumeSpace(at);
    return true;
}

void Game::bridgeButton(int id) {
    const bool paused = m_.u16(place::kPauseTicks) != 0;
    if (paused) {
        // The crew's main buttons go on with a paused script at once.
        if (id == button::kChekovCourse || id == button::kSpockReport || id == button::kKirkBeam ||
            id == button::kUhuraHail || id == button::kSuluOrbit) {
            m_.set16(place::kPauseTicks, 0);
            resumeSpace(m_.u16(ds::kResume));
        }
        return;
    }
    const bool attacked = m_.u8(place::kUnderAttack) != 0;
    const bool inOrbit = m_.u16(place::kInOrbit) != 0;
    const bool atDestination = m_.u16(ds::kSystem) == m_.u16(ds::kDestination);
    switch (id) {
    case button::kUhuraHail:
        if (!handle(handler::kHail)) {
            sayLine(kHailing);
        }
        break;
    case button::kSpockReport:
        if (!handle(handler::kReport)) {
            sayLine(kNothingToReport);
        }
        break;
    case button::kKirkBeam:
        if (!inOrbit) {
            sayLine(kNotInOrbit);
        } else if (!handle(handler::kBeam)) {
            sayLine(kRightNow);
        }
        break;
    case button::kSuluOrbit:
        if (attacked) {
            sayLine(kUnderAttack);
        } else if (atDestination && !inOrbit) {
            handle(handler::kOrbit);
            sayLine(kOrbiting);
        } else {
            sayLine(kNotDone);
        }
        break;
    case button::kChekovCourse:
        if (attacked) {
            sayLine(kUnderAttack);
        } else if (inOrbit) {
            sayLine(kLeaveOrbitFirst);
        } else {
            const int system = starMap();
            if (system < 0) {
                break;
            }
            m_.set16(ds::kSystem, static_cast<std::uint16_t>(system));
            if (m_.u16(ds::kSystem) == m_.u16(ds::kDestination)) {
                handle(handler::kArrived);
            }
        }
        break;
    default:
        break;
    }
}

void Game::bridge() {
    loadBridge();
    leaveBridge_ = false;
    p_.pointerShown = true;
    while (!p_.quit && !leaveBridge_) {
        // Time passes for the script: a paused script counts down, an
        // event loop gets its tick.
        while (ticksDue_ > 0 && !leaveBridge_) {
            --ticksDue_;
            if (m_.u16(ds::kResume) == 0) {
                resumeSpace(0);
                continue;
            }
            const auto pause = m_.u16(place::kPauseTicks);
            if (pause != 0) {
                m_.set16(place::kPauseTicks, static_cast<std::uint16_t>(pause - 1));
                if (pause == 1) {
                    resumeSpace(m_.u16(ds::kResume));
                }
            } else {
                handle(handler::kTick);
            }
        }
        drawBridge();
        p_.pointerTargets.clear();
        for (const auto& st : kStations) {
            const Sprite& s = crew_[st.actor];
            if (s.frames.empty()) {
                continue;
            }
            const auto& f = s.frames[s.frame];
            if (const auto pic = picture(f.picture)) {
                const int left = f.x - pic->handleX;
                const int top = f.y - pic->handleY;
                p_.pointerTargets.push_back(ClipRect{left, top, left + pic->width() - 1, top + pic->height() - 1});
            }
        }
        present();
        if ((p_.takeClicks() & 1) != 0) {
            const int who = crewAt(p_.mouseX, p_.mouseY);
            if (who >= 0) {
                const int id = chooseFromMenu(who);
                if (id >= 0) {
                    bridgeButton(id);
                }
            }
        }
        p_.keys.clear();
    }
}

void Game::run() {
    const auto fresh = assets_.source().read("SAVEGAME.NEW");
    if (!fresh) {
        throw FormatError("SAVEGAME.NEW is missing");
    }
    m_.loadSaved(*fresh);
    // A new game starts on the bridge with the first episode.
    m_.set16(ds::kState, 1);
    while (!p_.quit) {
        startEpisode();
        bridge();
        if (p_.quit) {
            break;
        }
        // On to the next episode.
        const int next = m_.s16(ds::kEpisode) + 1;
        static const char* const kEpisodes[] = {"fed", "sentnl", "noman", "light", "voids", "scotty", "madness", "meth"};
        if (next >= 8) {
            break;
        }
        m_.set16(ds::kEpisode, static_cast<std::uint16_t>(next));
        m_.setText(ds::kEpisodeName, kEpisodes[next], 0x10);
        m_.set16(ds::kResume, 0);
        m_.set16(place::kPauseTicks, 0);
    }
}

} // namespace

void playGame(Platform& platform, Assets& assets) {
    Game game(platform, assets);
    game.run();
}

} // namespace helmsman::sequel
