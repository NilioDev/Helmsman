// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_impl.hpp"

#include <sstream>

namespace helmsman::sequel {

namespace detail {

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

void Game::startEpisode(bool fresh) {
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
    if (fresh) {
        resetCrew();
        resetSpace();
    }
}

// What every episode starts from: the scripts' block empty, the usual
// equipment, the crew's records with their letters, colours and names
// (Kirk alone on the landing party).
void Game::resetCrew() {
    for (int i = 0; i < 0x12A; ++i) {
        m_.set8(static_cast<std::uint16_t>(ds::kScriptBlock + i), 0);
    }
    m_.set8(0xC221, m_.u8(ds::kCombatLevel));
    for (int item = 0x40; item < 0x80; ++item) {
        m_.set8(static_cast<std::uint16_t>(0x0F5A + 10 * item), item < 0x46 ? 1 : 0);
    }
    struct Member {
        char letter;
        std::uint8_t set;
        std::uint16_t name; // in the data segment
    };
    static constexpr Member kMembers[] = {{'k', 6, 0x2724}, {'s', 8, 0x2731}, {'m', 8, 0x273B},
                                          {'r', 3, 0x2745}, {'l', 6, 0x2756}, {'c', 6, 0x275F},
                                          {'u', 3, 0x276A}, {'y', 3, 0x2774}, {'b', 8, 0x277E}};
    for (int i = 0; i < 32; ++i) {
        const auto r = static_cast<std::uint16_t>(ds::kActors + i * ds::kActorSize);
        m_.set8(static_cast<std::uint16_t>(r + 0x48), 0);
        m_.set8(static_cast<std::uint16_t>(r + actor::kTeam), 0);
        m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) & ~8));
    }
    for (int i = 0; i < 9; ++i) {
        const auto r = static_cast<std::uint16_t>(ds::kActors + i * ds::kActorSize);
        const Member& mb = kMembers[i];
        m_.set8(static_cast<std::uint16_t>(r + 0x48), static_cast<std::uint8_t>(mb.letter));
        m_.set8(static_cast<std::uint16_t>(r + actor::kTextSet), mb.set);
        m_.set16(static_cast<std::uint16_t>(r + actor::kTextX), 0);
        m_.set16(static_cast<std::uint16_t>(r + actor::kTextY), 0);
        m_.set16(static_cast<std::uint16_t>(r + 0x68), 0);
        m_.setText(static_cast<std::uint16_t>(r + actor::kName), m_.text(mb.name, 24), 24);
    }
    m_.set8(static_cast<std::uint16_t>(ds::kActors + actor::kTeam), 0xBD);
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
    if (const auto onScreen = m_.text(kViewscreenName, 8); !onScreen.empty()) {
        viewscreenColours(onScreen);
    }
    shade_ = shadeTable(palette_, 45);
    for (const auto& st : kStations) {
        setSprite(crew_[st.actor], st.anim);
    }
}

// The colours of whoever is on the viewscreen: their palette file gives
// the colours their pictures use, the rest stay the bridge's.
void Game::viewscreenColours(const std::string& name) {
    const auto pal = file(name + ".PAL");
    if (!pal || pal->size() < 768) {
        return;
    }
    const auto theirs = Palette::fromBytes(std::span<const std::uint8_t>(*pal).first(768));
    auto take = [&](int from, int to) {
        for (int i = from; i < to; ++i) {
            palette_[static_cast<std::size_t>(i)] = theirs[static_cast<std::size_t>(i)];
        }
    };
    take(0x30, 0x80);
    take(0xD8, 0xDC);
    take(0xE0, 0xF6);
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
    p_.palette = palette_;
    if (space_ && fullView()) {
        drawSpace(true);
        return;
    }
    std::copy_n(background_.data(), background_.size(), p_.screen.data());
    drawSpace(!viewscreen_.shown);
    drawSprite(viewscreen_);
    for (int i = 0; i < kCrew; ++i) {
        drawSprite(talk_[i].shown ? talk_[i] : crew_[i]);
    }
    p_.palette = palette_;
}

void Game::present() {
    if (explore_) {
        exploreFrame();
    }
    tickSprites();
    p_.waitFrame();
    pumpTicks();
}

void Game::pumpTicks() {
    const double now = static_cast<double>(p_.millis);
    if (nextTick_ == 0) {
        nextTick_ = now;
    }
    int fresh = 0;
    while (now >= nextTick_) {
        nextTick_ += kTickMillis;
        ++ticksDue_;
        ++fresh;
    }
    if (away_) {
        // People in a room move on with every tick, also while a script
        // waits.
        for (int i = 0; i < std::min(fresh, 4); ++i) {
            ++awayTicks_;
            advanceBodies();
        }
    } else if (holdSpace_ == 0) {
        // So does space, also while the crew talks (not while a menu is
        // open).
        for (int i = 0; i < std::min(fresh, 4); ++i) {
            stepSpace();
        }
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
    HoldSpace hold(*this);
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
    return chooseButtons(menu(name), mx, my);
}

// Shows buttons over the scene at (mx, my) and waits for a click: the
// button's number, or -1 when the player backs out.
int Game::chooseButtons(const std::vector<MenuButton>& buttons, int mx, int my) {
    if (buttons.empty()) {
        return -1;
    }
    HoldSpace hold(*this);
    p_.takeClicks();
    p_.keys.clear();
    while (!p_.quit) {
        drawScene();
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
    const SoundBank* episodeBank = voices_ ? &*voices_ : nullptr;
    for (const SoundBank* bank : {computerVoices_, episodeBank}) {
        if (!sound && bank != nullptr && bank->has(tag)) {
            try {
                sound = bank->sound(tag);
            } catch (const FormatError&) {
            }
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
    if (speakerName_ != nullptr) {
        name = *speakerName_;
        set = 8;
        bx = 0x14;
        by = 0x14;
    }
    const bool menu = menuTitle_ != nullptr;
    if (menu) {
        name = *menuTitle_;
        set = 0;
        bx = 0x14;
        by = 0x14;
    }
    const bool choice = texts.size() > 1 || menu;
    if (explore_ && !menu) {
        // The explorer reads nothing and picks an answer by itself.
        if (p_.devTrace) {
            for (const auto& t : texts) {
                std::printf("  %s: %s\n", name.c_str(), t.c_str());
            }
        }
        return choice ? exploreChoice(static_cast<int>(texts.size())) : 0;
    }
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
        speaking_ = true;
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
        drawScene();
        const std::size_t first = choice ? 0 : page * kRowsPerPage;
        const std::size_t count = choice ? rows.size() : std::min<std::size_t>(kRowsPerPage, rows.size() - first);
        std::size_t widest = name.size();
        for (std::size_t i = 0; i < count; ++i) {
            widest = std::max(widest, rows[first + i].size());
        }
        const int w = static_cast<int>(widest) * SequelFont::kWidth + 12;
        const int h = static_cast<int>(count + (name.empty() ? 0 : 1)) * SequelFont::kHeight + 10;
        int x = std::clamp(bx, 2, 318 - w);
        int y = std::clamp(by, 2, 198 - h);
        if (speakerBelow_) {
            // Centred, at the foot of the screen.
            x = std::clamp(160 - w / 2, 2, 318 - w);
            y = std::max(2, 198 - h);
        }
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
        bool back = menu && (clicks & 2) != 0;
        for (const auto& k : p_.keys) {
            if (menu && k.scancode == scancode::kEscape) {
                back = true;
            } else if (k.scancode == scancode::kEnter || k.scancode == scancode::kSpace || k.scancode == scancode::kEscape) {
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
        if (back) {
            picked = -1;
            break;
        }
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
        speaking_ = false;
    }
    p_.pointerTargets.clear();
    return menu ? picked : std::max(0, picked);
}

int Game::menuBox(const std::string& title, const std::vector<std::string>& choices) {
    if (choices.empty()) {
        return -1;
    }
    HoldSpace hold(*this);
    menuTitle_ = &title;
    struct Reset {
        const std::string*& t;
        ~Reset() { t = nullptr; }
    } reset{menuTitle_};
    return say(-1, choices, 0);
}

int Game::sayAs(const std::string& name, const std::string& text) {
    speakerName_ = &name;
    struct Reset {
        const std::string*& t;
        ~Reset() { t = nullptr; }
    } reset{speakerName_};
    return say(-1, {text}, 0);
}

// The admiral's word on the episode just done (space command 0x13): how
// many of its points were earned sets the rating, which is kept for the
// captain's record and makes the ship a little stronger (see
// resetSpace()). `shown`: the admiral says it on the viewscreen.
void Game::evaluation(bool shown) {
    const int best = m_.s16(kEpisodePoints);
    const int got = std::clamp<int>(m_.s16(kEpisodeScore), 0, std::max(0, best));
    const int percent = best > 0 ? got * 100 / best : 0;
    // The ratings from worst (0) to best (4).
    static constexpr const char* kWords[5] = {
        "#EVAL005#I'll be frank Kirk, Starfleet expects more out of you than that.  If I were you, I'd do some "
        "serious soul-searching about my command ability. I would expect as much from a cadet.  But from "
        "Starfleet's finest... I'm going to have to forward my concerns to Command. Cain out.",
        "#EVAL004#While you acheived the desired results for your assignment, Jim, I can only give you a "
        "satifactory rating.  I would expect as much from a cadet.  I'm concerned about your performance and am "
        "going to have to forward my concerns to Command. Cain out.",
        "#EVAL003#Well done, Kirk. Keep up the good work. I must say, however, your performance is not what we've "
        "come to expect from James T. Kirk. I guess we all have our off days, Captain. Cain out.",
        "#EVAL002#The top brass at Starfleet are impressed with the results of your recent performance.  "
        "Outstanding work, Jim.  Keep up the good work. Cain out.",
        "#EVAL001#I am very pleased with your performance. It was a perfect mission, Jim! Your reputation as "
        "Starfleet's best starship captain is secure. Cain out.",
    };
    const int rating = percent <= 50 ? 0 : percent <= 70 ? 1 : percent <= 85 ? 2 : percent <= 99 ? 3 : 4;
    static constexpr int kSongs[5] = {9, 9, 0x0C, 0x0E, 0x0A};
    if (shown) {
        music(kSongs[rating], -1);
        setSprite(viewscreen_, "WOMAN");
        viewscreen_.x = kViewscreenX;
        viewscreen_.y = kViewscreenY;
        viewscreen_.still = true;
        viewscreen_.shown = !viewscreen_.frames.empty();
        viewscreenTalks_ = true;
        m_.setText(kViewscreenName, "woman", 8);
        viewscreenColours("woman");
        const std::string admiral = "Admiral Cain";
        speakerBelow_ = true;
        struct Below {
            bool& b;
            ~Below() { b = false; }
        } below{speakerBelow_};
        pendingTalker_ = kViewscreenSpeaker;
        sayAs(admiral, "#EVAL000#I've reviewed your report from your recent assignment, Captain and have a few comments.");
        pendingTalker_ = kViewscreenSpeaker;
        sayAs(admiral, kWords[rating]);
        viewscreen_.shown = false;
        viewscreenTalks_ = false;
    }
    const int episode = m_.s16(ds::kEpisode);
    if (episode >= 0 && episode < 8) {
        m_.set16(static_cast<std::uint16_t>(kRatings + 2 * episode), static_cast<std::uint16_t>(rating));
        m_.set16(kRatingSum, static_cast<std::uint16_t>(m_.s16(kRatingSum) + rating));
        m_.set16(kRatingPercent, static_cast<std::uint16_t>(m_.s16(kRatingSum) * 100 / ((episode + 1) * 4)));
    }
    // Scotty's extra power lasts longer, and the ship is a little better.
    m_.set16(fight::kBoostTicks, static_cast<std::uint16_t>(m_.s16(fight::kBoostTicks) + rating * 15));
    m_.set16(fight::kBoostSpare, static_cast<std::uint16_t>(m_.s16(fight::kBoostSpare) + rating / 2));
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
    if (p_.devTrace && c.function != cmd::kPause) {
        std::fprintf(stderr, "space %02x (%04x %04x %04x)\n", c.function, c.arg(0), c.arg(1), c.arg(2));
    }
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
        // Someone new on the viewscreen brings their own colours, and
        // perhaps a song (the second number, -1 for none).
        if (!name.empty() && upperName(name) != upperName(m_.text(kViewscreenName, 8))) {
            m_.setText(kViewscreenName, name, 8);
            viewscreenColours(name);
            if (c.sarg(1) != -1) {
                music(c.sarg(1), -1);
            }
        }
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
        if (p_.devTrace) {
            std::fprintf(stderr, "away mission: room %d, way in %d\n", c.sarg(1), c.sarg(2));
        }
        m_.set16(ds::kRoom, c.arg(1));
        m_.set16(ds::kSpawn, c.arg(2));
        m_.set16(place::kPauseTicks, 1);
        awayPending_ = true;
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
        // The game is saved here (the script goes on from the next step
        // after a tick); a lost mission comes back to it.
        m_.set16(place::kPauseTicks, 1);
        writeCheckpoint(m_.scriptText(seg, c.arg(0), 10));
        return Machine::Flow::Yield;
    case cmd::kBackToCheckpoint: {
        const std::string name = m_.scriptText(seg, c.arg(0), 10);
        if (auto data = readCheckpoint(name)) {
            throw RestoreGame{std::move(*data), c.sarg(1)};
        }
        return Machine::Flow::Go;
    }
    case cmd::kRestored:
        // What the checkpoint was come back to with (0 when just saved).
        result = m_.u16(kRestoreCode);
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
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x10:
        result = shipCommand(c);
        return Machine::Flow::Go;
    case 0x13:
        // The episode is judged (a name of the place first; nothing is
        // said without one).
        evaluation(!m_.scriptText(seg, c.arg(0), 0x80).empty());
        return Machine::Flow::Go;
    case 0x2A:
        gameOverMenu();
        return Machine::Flow::Go;
    case 0x2E: { // the bridge's colours change (the lights go out)
        const auto name = m_.scriptText(seg, c.arg(0), 10);
        if (const auto pal = file(name + ".PAL"); pal && pal->size() >= 768) {
            palette_ = Palette::fromBytes(std::span<const std::uint8_t>(*pal).first(768));
        }
        return Machine::Flow::Go;
    }
    case 0x11: { // a person on the bridge does something: (who, animation, x, y, wait)
        const int who = c.sarg(0);
        const auto name = m_.scriptText(seg, c.arg(1), 10);
        if (who >= 0 && who < kCrew && !name.empty() && static_cast<std::uint8_t>(name[0]) != 0xFF) {
            setSprite(crew_[who], name);
            crew_[who].x = c.sarg(2);
            crew_[who].y = c.sarg(3);
        }
        result = static_cast<std::uint32_t>(static_cast<std::uint16_t>(who));
        return Machine::Flow::Go;
    }
    case 0x0F: // to another star system at once
        m_.set16(ds::kSystem, c.arg(0));
        return Machine::Flow::Go;
    case 0x2B: // shields up, weapons armed: ready for a fight
    case 0x2C:
        result = shipCommand(c);
        return Machine::Flow::Go;
    case cmd::kSoundEffect: // a sound effect: (number, volume, pan)
        soundEffect(c.sarg(0), c.arg(1) & 0xFF, c.arg(2) & 0xFF);
        return Machine::Flow::Go;
    case cmd::kMusic: // music: (the song now, the one to go on with)
        music(c.sarg(0), c.sarg(1));
        return Machine::Flow::Go;
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
    if (id == button::kKirkOptions) {
        optionsMenu();
        return;
    }
    if (id == button::kKirkLog || id == button::kSpockComputer) {
        HoldSpace hold(*this);
        if (id == button::kKirkLog) {
            captainsLog();
        } else {
            libraryComputer();
        }
        return;
    }
    if (combatButton(id)) {
        return;
    }
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
    if (!explore_ || !awayPending_) {
        loadBridge(); // (the explorer goes straight back to its mission)
    }
    loadSuite(episodeName_, 's');
    leaveBridge_ = false;
    p_.pointerShown = true;
    while (!p_.quit && !leaveBridge_) {
        if (awayPending_) {
            // To an away mission, and back to the bridge after it.
            awayPending_ = false;
            awayMission();
            loadBridge();
            loadSuite(episodeName_, 's');
            ticksDue_ = 0;
            p_.pointerShown = true;
            bridgePointer();
        }
        // The original counts no time while a line is shown or a menu is
        // open: what piled up meanwhile is let go.
        ticksDue_ = std::min(ticksDue_, 2);
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
        spaceReports();
        if (space_ && space_->playerDestroyed()) {
            gameOverMenu();
        }
        drawBridge();
        p_.pointerTargets.clear();
        for (const auto& st : kStations) {
            if (fullView()) {
                break;
            }
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
        bool options = false;
        std::deque<KeyPress> keys;
        keys.swap(p_.keys);
        for (const auto& k : keys) {
            if (k.scancode == scancode::kEscape) {
                if (fullView()) {
                    // Escape leaves the viewscreen for the bridge.
                    setFullView(false);
                    setManual(false);
                } else {
                    options = true;
                }
                continue;
            }
            combatKey(k);
            if (leaveBridge_ || awayPending_) {
                break;
            }
        }
        if (options) {
            optionsMenu();
            continue;
        }
        const auto clicks = p_.takeClicks();
        if (combatClick(clicks)) {
            continue;
        }
        if ((clicks & 1) != 0) {
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

// Before a new game: a saved one may be loaded instead (loadMenu goes
// there by throwing RestoreGame), then the fights are made as hard as the
// player likes.
void Game::startMenu() {
    loadBridge();
    // The title's theme (its first song is only a short call).
    loadSuite("title", 0);
    music(1, 1);
    p_.pointerShown = true;
    while (!p_.quit && anySaves()) {
        const int choice =
            menuBox("Start Game", {"#GENER006#Load a previously saved game.", "#GENER020#Restart the game."});
        if (choice == 1 || choice < 0) {
            break;
        }
        loadMenu();
    }
    const int level = menuBox("Space Combat Level", {"Federation Cadet (None).", "Academy Graduate (Easy).",
                                                     "Commissioned Officer (Hard)."});
    combatLevel_ = level < 0 ? 2 : level;
}

void Game::newGame() {
    const auto fresh = assets_.source().read("SAVEGAME.NEW");
    if (!fresh) {
        throw FormatError("SAVEGAME.NEW is missing");
    }
    m_.loadSaved(*fresh);
    if (combatLevel_ >= 0) {
        m_.set16(ds::kCombatLevel, static_cast<std::uint16_t>(combatLevel_));
    }
    // A new game starts on the bridge with the first episode.
    m_.set16(ds::kState, 1);
    startEpisode();
}

void Game::run() {
    std::optional<RestoreGame> pending;
    while (!p_.quit) {
        try {
            if (pending) {
                restorePayload(pending->payload);
                if (pending->code >= 0) {
                    m_.set16(kRestoreCode, static_cast<std::uint16_t>(pending->code));
                }
                pending.reset();
            } else {
                if (p_.devStart.empty()) {
                    startMenu();
                }
                newGame();
                if (!p_.devStart.empty()) {
                    if (devStart()) {
                        return;
                    }
                }
            }
            episodes();
            return;
        } catch (RestoreGame& r) {
            pending = std::move(r);
        } catch (const RestartGame&) {
            pending.reset();
        }
        away_ = false;
        awayPending_ = false;
    }
}

// For the tools: straight to an away mission ("episode room spawn"), or
// to a place in an episode's space script ("space episode offset", the
// offset in hex). True when the game is over after it.
bool Game::devStart() {
    std::istringstream in(p_.devStart);
    std::string first;
    in >> first;
    if (first == "explore") {
        // "explore episode room spawn most [orders to give first]"
        std::string ep;
        int room = 0;
        int spawn = -1;
        int most = 3000;
        in >> ep >> room >> spawn >> most;
        std::string path;
        std::getline(in, path);
        startExplorer(most, path);
        devAwayMission(ep + " " + std::to_string(room) + " " + std::to_string(spawn));
        return true;
    }
    if (first != "space") {
        devAwayMission(p_.devStart);
        return true;
    }
    std::string ep;
    std::string at;
    int level = 1;
    in >> ep >> at >> level;
    m_.set16(ds::kCombatLevel, static_cast<std::uint16_t>(std::clamp(level, 0, 2)));
    static const char* const kNames[] = {"fed", "sentnl", "noman", "light", "voids", "scotty", "madness", "meth"};
    for (int i = 0; i < 8; ++i) {
        if (ep == kNames[i]) {
            m_.set16(ds::kEpisode, static_cast<std::uint16_t>(i));
            m_.setText(ds::kEpisodeName, ep, 0x10);
        }
    }
    startEpisode();
    loadBridge();
    resumeSpace(static_cast<std::uint16_t>(std::stoul(at, nullptr, 16)));
    return false;
}

void Game::devAwayMission(const std::string& spec) {
    std::istringstream in(spec);
    std::string ep;
    int room = 0;
    int spawn = -1;
    in >> ep >> room >> spawn;
    static const char* const kNames[] = {"fed", "sentnl", "noman", "light", "voids", "scotty", "madness", "meth"};
    for (int i = 0; i < 8; ++i) {
        if (ep == kNames[i]) {
            m_.set16(ds::kEpisode, static_cast<std::uint16_t>(i));
            m_.setText(ds::kEpisodeName, ep, 0x10);
        }
    }
    startEpisode();
    m_.set16(ds::kRoom, static_cast<std::uint16_t>(room));
    m_.set16(ds::kSpawn, static_cast<std::uint16_t>(spawn));
    // The usual landing party.
    for (int i = 0; i < 3; ++i) {
        const auto r = static_cast<std::uint16_t>(ds::kActors + i * ds::kActorSize);
        m_.set8(static_cast<std::uint16_t>(r + actor::kTeam), 0xBD);
        m_.set8(r, static_cast<std::uint8_t>(m_.u8(r) | 8));
    }
    awayMission();
}

// The episodes one after another, from the one the game is at.
void Game::episodes() {
    while (!p_.quit) {
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
        startEpisode();
    }
}

} // namespace detail

void playGame(Platform& platform, Assets& assets) {
    detail::Game game(platform, assets);
    game.run();
}

} // namespace helmsman::sequel
