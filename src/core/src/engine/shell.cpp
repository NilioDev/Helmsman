// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/shell.hpp"

#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/ui_font.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace helmsman {

namespace {

// Helmsman's own colours on its own screens.
enum Ink : std::uint8_t {
    kBlack = 0,
    kStarDim = 1,
    kStar = 2,
    kStarBright = 3,
    kText = 4,
    kDim = 5,
    kGold = 6,
    kPanel = 7,
    kPanelLit = 8,
    kEdge = 9,
    kEdgeLit = 10,
    kRed = 11,
};

Palette shellPalette() {
    Palette p;
    const std::array<Colour, 12> colours = {{
        {0, 0, 0},
        {14, 14, 20},
        {30, 30, 38},
        {58, 58, 63},
        {58, 58, 54},
        {34, 36, 42},
        {63, 50, 12},
        {6, 9, 18},
        {10, 18, 34},
        {18, 24, 40},
        {40, 52, 63},
        {63, 22, 18},
    }};
    for (std::size_t i = 0; i < colours.size(); ++i) {
        p[i] = colours[i];
    }
    return p;
}

constexpr GameId kOrder[] = {GameId::Anniversary, GameId::JudgmentRites};
constexpr int kCards = 2;
constexpr int kQuit = kCards; // the third choice
constexpr ClipRect kCardRects[kCards] = {{20, 52, 156, 150}, {164, 52, 300, 150}};
constexpr ClipRect kQuitRect{130, 178, 190, 191};

// A slowly drifting field of stars, made up here.
struct Stars {
    struct Star {
        float x;
        float y;
        float speed;
        std::uint8_t ink;
    };
    std::array<Star, 90> stars{};
    Stars() {
        std::uint32_t seed = 0x1234567u;
        auto next = [&seed] {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 8) & 0xFFFF;
        };
        for (auto& s : stars) {
            s.x = static_cast<float>(next() % 320);
            s.y = static_cast<float>(next() % 200);
            const auto depth = next() % 3;
            s.speed = 2.0f + 6.0f * static_cast<float>(depth);
            s.ink = static_cast<std::uint8_t>(kStarDim + depth);
        }
    }
    void draw(Surface& to, double seconds) const {
        for (const auto& s : stars) {
            const double x = std::fmod(s.x - seconds * s.speed + 3200.0, 320.0);
            to.set(static_cast<int>(x), static_cast<int>(s.y), s.ink);
        }
    }
};

void frame(Surface& to, const ClipRect& r, bool lit) {
    to.fillRect(r.left, r.top, r.right - r.left, r.bottom - r.top, lit ? kPanelLit : kPanel);
    const std::uint8_t edge = lit ? kEdgeLit : kEdge;
    to.fillRect(r.left, r.top, r.right - r.left, 1, edge);
    to.fillRect(r.left, r.bottom - 1, r.right - r.left, 1, edge);
    to.fillRect(r.left, r.top, 1, r.bottom - r.top, edge);
    to.fillRect(r.right - 1, r.top, 1, r.bottom - r.top, edge);
}

// Text cut to fit `width` pixels, with "..." at the front when cut.
std::string fitText(const std::string& text, int width) {
    const auto most = static_cast<std::size_t>(std::max(width / ui::kCellW, 4));
    if (text.size() <= most) {
        return text;
    }
    return "..." + text.substr(text.size() - (most - 3));
}

// Words wrapped to lines of `width` pixels.
std::vector<std::string> wrap(const std::string& text, int width) {
    const auto most = static_cast<std::size_t>(std::max(width / ui::kCellW, 4));
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        auto space = text.find(' ', at);
        if (space == std::string::npos) {
            space = text.size();
        }
        const std::string word = text.substr(at, space - at);
        if (!line.empty() && line.size() + 1 + word.size() > most) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        at = space + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// A big title: each pixel of the small font drawn 2 by 2.
void drawBig(Surface& to, int cx, int y, std::string_view text, std::uint8_t ink) {
    Surface small(ui::textWidth(text), ui::kCellH);
    ui::drawText(small, 0, 0, text, 1);
    const int left = cx - small.width();
    for (int row = 0; row < small.height(); ++row) {
        for (int col = 0; col < small.width(); ++col) {
            if (small.at(col, row) != 0) {
                to.fillRect(left + col * 2, y + row * 2, 2, 2, ink);
            }
        }
    }
}

} // namespace

const char* gameTitle(GameId game) {
    switch (game) {
    case GameId::Anniversary: return "25th Anniversary";
    case GameId::JudgmentRites: return "Judgment Rites";
    }
    return "";
}

const char* gameYear(GameId game) {
    switch (game) {
    case GameId::Anniversary: return "1992, CD-ROM 1996";
    case GameId::JudgmentRites: return "1993, CD-ROM 1994";
    }
    return "";
}

std::optional<GameId> chooseGame(Platform& platform, Library& library) {
    const Stars stars;
    platform.palette = shellPalette();
    platform.pointerShown = false;
    int focus = 0;
    int lastX = platform.mouseX;
    int lastY = platform.mouseY;
    const std::uint64_t start = platform.millis;
    platform.takeClicks();
    for (;;) {
        const auto games = library.games();
        auto entryFor = [&](GameId id) -> const GameEntry* {
            for (const auto& g : games) {
                if (g.game == id) {
                    return &g;
                }
            }
            return nullptr;
        };

        // The pointer, moved by the mouse or the gamepad, sets the focus.
        auto choiceAt = [&](int x, int y) {
            for (int i = 0; i < kCards; ++i) {
                if (kCardRects[i].contains(x, y)) {
                    return i;
                }
            }
            return kQuitRect.contains(x, y) ? kQuit : -1;
        };
        if (platform.mouseX != lastX || platform.mouseY != lastY) {
            lastX = platform.mouseX;
            lastY = platform.mouseY;
            if (const int at = choiceAt(lastX, lastY); at >= 0) {
                focus = at;
            }
        }
        int picked = -1;
        if ((platform.takeClicks() & 1) != 0) {
            picked = choiceAt(platform.mouseX, platform.mouseY);
        }
        while (!platform.keys.empty()) {
            const auto key = platform.keys.front();
            platform.keys.pop_front();
            switch (key.scancode) {
            case scancode::kLeft: focus = focus == kQuit ? 0 : std::max(0, focus - 1); break;
            case scancode::kRight: focus = focus == kQuit ? kCards - 1 : std::min(kCards - 1, focus + 1); break;
            case scancode::kDown: focus = kQuit; break;
            case scancode::kUp:
                if (focus == kQuit) {
                    focus = 0;
                }
                break;
            case scancode::kTab: focus = (focus + 1) % (kCards + 1); break;
            case scancode::kEnter:
            case scancode::kSpace: picked = focus; break;
            case scancode::kEscape: return std::nullopt;
            default:
                if (key.text == '1' || key.text == '2') {
                    picked = key.text - '1';
                }
                break;
            }
        }
        if (picked == kQuit) {
            return std::nullopt;
        }
        if (picked >= 0) {
            focus = picked;
            if (entryFor(kOrder[picked]) != nullptr) {
                return kOrder[picked];
            }
        }

        // Drawing.
        Surface& s = platform.screen;
        s.fill(kBlack);
        stars.draw(s, static_cast<double>(platform.millis - start) / 1000.0);
        drawBig(s, 160, 10, "HELMSMAN", kGold);
        ui::drawCentred(s, 160, 34, "Choose your voyage", kDim);
        platform.pointerTargets.clear();
        for (int i = 0; i < kCards; ++i) {
            const auto& r = kCardRects[i];
            const GameId id = kOrder[i];
            const auto* entry = entryFor(id);
            const bool lit = focus == i;
            frame(s, r, lit);
            const int cx = (r.left + r.right) / 2;
            ui::drawCentred(s, cx, r.top + 12, gameTitle(id), lit ? kGold : kText);
            ui::drawCentred(s, cx, r.top + 24, gameYear(id), kDim);
            if (entry != nullptr) {
                ui::drawCentred(s, cx, r.top + 50, "Ready to play", kText);
                ui::drawCentred(s, cx, r.top + 62, fitText(entry->where, r.right - r.left - 12), kDim);
                ui::drawCentred(s, cx, r.bottom - 16, lit ? "> Start <" : "Start", lit ? kGold : kText);
            } else {
                int y = r.top + 44;
                for (const auto& line : wrap("Not added yet. Drop your CD image (.cue, .iso) or CD folder on "
                                             "this window.",
                                             r.right - r.left - 12)) {
                    ui::drawCentred(s, cx, y, line, kDim);
                    y += ui::kCellH + 1;
                }
            }
            platform.pointerTargets.push_back(r);
        }
        frame(s, kQuitRect, focus == kQuit);
        ui::drawCentred(s, 160, kQuitRect.top + 3, "Quit", focus == kQuit ? kGold : kText);
        platform.pointerTargets.push_back(kQuitRect);
        if (!library.note.empty()) {
            int y = 156;
            for (const auto& line : wrap(library.note, 300)) {
                ui::drawCentred(s, 160, y, line, kRed);
                y += ui::kCellH;
            }
        }
        platform.waitFrame();
    }
}

void runShell(Platform& platform, Library& library) {
    for (;;) {
        const auto picked = chooseGame(platform, library);
        if (!picked) {
            platform.quit = true;
            return;
        }
        std::unique_ptr<GameSource> source;
        try {
            source = library.open(*picked);
        } catch (const FormatError& e) {
            library.note = e.what();
            continue;
        }
        if (!source) {
            library.note = std::string(gameTitle(*picked)) + " cannot be opened.";
            continue;
        }
        library.note.clear();
        try {
            library.play(*source);
        } catch (const FormatError& e) {
            library.note = e.what();
        }
    }
}

} // namespace helmsman
