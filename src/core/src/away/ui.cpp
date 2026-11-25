// SPDX-License-Identifier: GPL-3.0-or-later
//
// The games' boxes and menus over a scene: text boxes with their buttons and
// choices, and rows of picture buttons. Shared by the planet missions and
// the bridge.
#include "helmsman/away/ui.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cstdio>

namespace helmsman::away {

namespace {

constexpr int kTextColumns = 24;

std::vector<std::string> wrapWords(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        // The next word, with the spaces before it.
        std::size_t j = i;
        while (j < text.size() && text[j] == ' ') {
            ++j;
        }
        std::size_t k = j;
        while (k < text.size() && text[k] != ' ') {
            ++k;
        }
        const std::string spaces = line.empty() ? std::string{} : text.substr(i, j - i);
        std::string word = text.substr(j, k - j);
        if (static_cast<int>(line.size() + spaces.size() + word.size()) <= width) {
            line += spaces + word;
        } else {
            if (!line.empty()) {
                lines.push_back(line);
            }
            line.clear();
            while (static_cast<int>(word.size()) > width) {
                lines.push_back(word.substr(0, static_cast<std::size_t>(width)));
                word.erase(0, static_cast<std::size_t>(width));
            }
            line = word;
        }
        i = k;
    }
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

void setPointer(Session& session, const std::string& name) {
    try {
        const auto bitmap = session.assets.picture(name);
        Platform& p = session.platform;
        p.pointer = bitmap->pixels;
        p.pointerHotX = bitmap->handleX;
        p.pointerHotY = bitmap->handleY;
    } catch (const FormatError&) {
    }
}

int textBox(Ui& ui, const std::vector<std::string>& lines, int x, int y, std::uint8_t colour) {
    if (lines.size() < 2) {
        return 0;
    }
    Platform& p = ui.session.platform;
    const std::string& speaker = lines[0];
    const int choices = static_cast<int>(lines.size()) - 1;
    // Voice only: no box for a single spoken text.
    if (choices == 1 && ui.session.textMode == 2) {
        const SpokenText spoken = splitSpoken(lines[1]);
        if (!spoken.voice.empty()) {
            ui.session.playSpeech(spoken.voice);
            p.takeClicks();
            p.keys.clear();
            int guard = 0;
            while ((ui.session.speaking() || guard < 4) && !p.quit) {
                ++guard;
                ui.redraw();
                ui.session.waitTick();
                if (p.takeClicks() != 0 || !p.keys.empty()) {
                    p.keys.clear();
                    break;
                }
            }
            ui.session.stopSpeech();
            return 0;
        }
    }
    std::vector<SpokenText> texts;
    std::vector<std::vector<std::string>> wrapped;
    int maxLines = 1;
    for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
        texts.push_back(splitSpoken(lines[static_cast<std::size_t>(i)]));
        wrapped.push_back(wrapWords(texts.back().words, kTextColumns));
        maxLines = std::max(maxLines, static_cast<int>(wrapped.back().size()));
    }
    const int visible = std::min(maxLines, 12);
    const bool header = !speaker.empty();
    const int rows = visible + (header ? 4 : 2);
    const int w = 26 * 8;
    const int h = rows * 8;

    // Where the box goes: near (x, y), kept on the screen.
    if (319 - x - (w + 29) / 2 < 0) {
        x += 319 - x - (w + 29) / 2;
    }
    if (x - (w + 29) / 2 < 1) {
        x -= x - (w + 29) / 2 - 1;
    }
    if (y - (h + 17) - 20 < 0) {
        y -= y - (h + 17) - 20;
    }
    const int left = x - (w + 29) / 2;
    const int top = y - h;

    Sprite box;
    box.mode = SpriteMode::Text;
    box.columns = 26;
    box.x = left;
    box.y = top;
    box.depth = 15;
    box.order = 8;
    box.textColour = colour;

    // The buttons beside the box.
    struct Button {
        std::string picture;
        int x = 0;
        int y = 0;
        int value = 0;
        std::shared_ptr<const Bitmap> bitmap;
    };
    std::vector<Button> buttons;
    if (const auto menu = ui.session.archive.load("TEXTBTNS.MNU")) {
        Reader r(*menu);
        while (r.remaining() >= 16) {
            Button b;
            b.picture = lower(r.fixedText(10));
            while (!b.picture.empty() && b.picture.back() == ' ') {
                b.picture.pop_back();
            }
            b.x = r.s16() + left + 0x96;
            b.y = r.s16() + top - 0x11;
            b.value = r.s16();
            try {
                b.bitmap = ui.session.assets.picture(b.picture);
            } catch (const FormatError&) {
                continue;
            }
            buttons.push_back(std::move(b));
        }
    }

    int choice = 0;
    int scroll = 0;
    int shownChoice = -1;
    auto enabled = [&](int value) {
        switch (value) {
        case 0:
            return true;
        case 1:
            return scroll > 0;
        case 2:
            return scroll + visible < static_cast<int>(wrapped[static_cast<std::size_t>(choice)].size());
        case 3:
        case 4:
            return choices > 1;
        default:
            return false;
        }
    };
    auto fill = [&]() {
        box.text.assign(static_cast<std::size_t>(26 * rows), ' ');
        auto put = [&](int r, int c, std::uint8_t ch) { box.text[static_cast<std::size_t>(r * 26 + c)] = ch; };
        for (int r = 0; r < rows; ++r) {
            put(r, 0, 0x13);
            put(r, 25, 0x14);
        }
        for (int c = 1; c < 25; ++c) {
            put(0, c, 0x11);
            put(rows - 1, c, 0x16);
        }
        put(0, 0, 0x10);
        put(0, 25, 0x12);
        put(rows - 1, 0, 0x15);
        put(rows - 1, 25, 0x17);
        int row = 1;
        if (header) {
            std::string title = speaker;
            if (choices > 1) {
                char buf[32];
                std::snprintf(buf, sizeof buf, " choice %d", choice + 1);
                title += buf;
            }
            if (static_cast<int>(title.size()) > kTextColumns) {
                title.resize(kTextColumns);
            }
            const int at = (kTextColumns - static_cast<int>(title.size())) / 2;
            for (std::size_t i = 0; i < title.size(); ++i) {
                put(1, 1 + at + static_cast<int>(i), static_cast<std::uint8_t>(title[i]));
            }
            for (int c = 1; c < 25; ++c) {
                put(2, c, 0x19);
            }
            row = 3;
        }
        const auto& text = wrapped[static_cast<std::size_t>(choice)];
        for (int i = 0; i < visible; ++i) {
            const int li = scroll + i;
            if (li >= static_cast<int>(text.size())) {
                break;
            }
            const auto& line = text[static_cast<std::size_t>(li)];
            for (std::size_t c = 0; c < line.size() && c < static_cast<std::size_t>(kTextColumns); ++c) {
                put(row + i, 1 + static_cast<int>(c), static_cast<std::uint8_t>(line[c]));
            }
        }
    };

    const auto savedPointer = p.pointer;
    const int savedHotX = p.pointerHotX;
    const int savedHotY = p.pointerHotY;
    setPointer(ui.session, "pushbtn");
    p.warpPointer = true;
    p.warpX = left + 0xDE;
    p.warpY = top - 8;
    p.takeClicks();
    p.keys.clear();

    bool done = false;
    bool heardVoice = false;
    while (!done) {
        if (shownChoice != choice) {
            shownChoice = choice;
            const auto& spoken = texts[static_cast<std::size_t>(choice)];
            if (!spoken.voice.empty()) {
                ui.session.playSpeech(spoken.voice);
            }
        }
        fill();
        ui.redraw();
        ui.scene.drawSprite(p.screen, box);
        p.pointerTargets.clear();
        for (const auto& b : buttons) {
            if (!enabled(b.value)) {
                continue;
            }
            b.bitmap->draw(p.screen, b.x, b.y);
            p.pointerTargets.push_back(ClipRect{b.x - b.bitmap->handleX, b.y - b.bitmap->handleY,
                                                b.x - b.bitmap->handleX + b.bitmap->width(),
                                                b.y - b.bitmap->handleY + b.bitmap->height()});
        }
        ui.session.waitTick();
        int pressed = -1;
        // With subtitles, a single spoken text goes away when the voice ends.
        if (ui.session.textMode == 1 && choices == 1 && !texts[0].voice.empty()) {
            if (ui.session.speaking()) {
                heardVoice = true;
            } else if (heardVoice) {
                pressed = 0;
            }
        }
        if ((p.takeClicks() & 1) != 0) {
            for (const auto& b : buttons) {
                if (enabled(b.value) && p.mouseX >= b.x - b.bitmap->handleX && p.mouseX < b.x - b.bitmap->handleX + b.bitmap->width() &&
                    p.mouseY >= b.y - b.bitmap->handleY && p.mouseY < b.y - b.bitmap->handleY + b.bitmap->height()) {
                    pressed = b.value;
                }
            }
        }
        while (!p.keys.empty()) {
            const KeyPress key = p.keys.front();
            p.keys.pop_front();
            switch (key.scancode) {
            case scancode::kEnter:
            case scancode::kEscape:
            case scancode::kSpace:
                pressed = 0;
                break;
            case scancode::kUp:
                pressed = 1;
                break;
            case scancode::kDown:
                pressed = 2;
                break;
            case scancode::kLeft:
                pressed = 3;
                break;
            case scancode::kRight:
                pressed = 4;
                break;
            default:
                break;
            }
        }
        if (pressed == 0) {
            done = true;
        } else if (pressed == 1 && enabled(1)) {
            --scroll;
        } else if (pressed == 2 && enabled(2)) {
            ++scroll;
        } else if (pressed == 3 && choices > 1) {
            choice = (choice + choices - 1) % choices;
            scroll = 0;
        } else if (pressed == 4 && choices > 1) {
            choice = (choice + 1) % choices;
            scroll = 0;
        }
        if (p.quit) {
            break;
        }
    }
    ui.session.stopSpeech();
    p.pointer = savedPointer;
    p.pointerHotX = savedHotX;
    p.pointerHotY = savedHotY;
    p.pointerTargets.clear();
    p.takeClicks();
    return choice;
}

// A row of picture buttons from a menu file (.MNU: name, x, y, value),
// placed from (x, y). Buttons whose bit (1 << (value + 1)) is in `hidden`
// are left out. Gives the value of the button chosen, or -1.
int buttonMenu(Ui& ui, const std::string& file, int x, int y, std::uint32_t hidden) {
    Platform& p = ui.session.platform;
    struct Button {
        Sprite sprite;
        int value = 0;
    };
    std::vector<Button> buttons;
    if (const auto menu = ui.session.archive.load(upper(file) + ".MNU")) {
        Reader r(*menu);
        while (r.remaining() >= 16) {
            std::string name = lower(r.fixedText(10));
            while (!name.empty() && name.back() == ' ') {
                name.pop_back();
            }
            Button b;
            b.sprite.x = x + r.s16();
            b.sprite.y = y + r.s16();
            b.value = r.s16();
            if (b.value >= -1 && b.value < 31 && (hidden & (1u << (b.value + 1))) != 0) {
                continue;
            }
            try {
                b.sprite.bitmap = ui.session.assets.picture(name);
            } catch (const FormatError&) {
                continue;
            }
            b.sprite.mode = SpriteMode::Overlay;
            b.sprite.depth = 15;
            buttons.push_back(std::move(b));
        }
    }
    if (buttons.empty()) {
        return -1;
    }
    const auto savedPointer = p.pointer;
    const int savedHotX = p.pointerHotX;
    const int savedHotY = p.pointerHotY;
    setPointer(ui.session, "pushbtn");
    p.takeClicks();
    p.keys.clear();
    p.pointerTargets.clear();
    for (const auto& b : buttons) {
        int l = 0, t = 0, w = 0, h = 0;
        if (b.sprite.bounds(l, t, w, h)) {
            p.pointerTargets.push_back(ClipRect{l, t, l + w, t + h});
        }
    }
    p.warpPointer = true;
    p.warpX = buttons[0].sprite.x + buttons[0].sprite.bitmap->width() / 2;
    p.warpY = buttons[0].sprite.y + buttons[0].sprite.bitmap->height() / 2;
    auto under = [&](int mx, int my) {
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            int l = 0, t = 0, w = 0, h = 0;
            if (buttons[i].sprite.bounds(l, t, w, h) && mx >= l && my >= t && mx < l + w && my < t + h) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    int result = -1;
    bool done = false;
    while (!done) {
        ui.redraw();
        const int hover = under(p.mouseX, p.mouseY);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const Sprite& sp = buttons[i].sprite;
            ui.scene.drawSprite(p.screen, sp);
            if (static_cast<int>(i) == hover) {
                // The button under the pointer gets a frame.
                int l = 0, t = 0, w = 0, h = 0;
                sp.bounds(l, t, w, h);
                for (int c = l + 1; c < l + w - 1; ++c) {
                    p.screen.set(c, t + 1, 0xDA);
                    p.screen.set(c, t + h - 2, 0xDA);
                }
                for (int r = t + 1; r < t + h - 1; ++r) {
                    p.screen.set(l + 1, r, 0xDA);
                    p.screen.set(l + w - 2, r, 0xDA);
                }
            }
        }
        ui.session.waitTick();
        const std::uint8_t clicks = p.takeClicks();
        if ((clicks & 1) != 0) {
            const int i = under(p.mouseX, p.mouseY);
            result = i >= 0 ? buttons[static_cast<std::size_t>(i)].value : -1;
            done = true;
        } else if ((clicks & 2) != 0) {
            done = true;
        }
        while (!p.keys.empty()) {
            const KeyPress key = p.keys.front();
            p.keys.pop_front();
            if (key.scancode == scancode::kEnter || key.scancode == scancode::kF1) {
                const int i = under(p.mouseX, p.mouseY);
                result = i >= 0 ? buttons[static_cast<std::size_t>(i)].value : -1;
                done = true;
            } else if (key.scancode == scancode::kEscape || key.scancode == scancode::kSpace) {
                done = true;
            }
        }
        if (p.quit) {
            break;
        }
    }
    ui.session.playEffect("BUTTON1");
    p.pointer = savedPointer;
    p.pointerHotX = savedHotX;
    p.pointerHotY = savedHotY;
    p.pointerTargets.clear();
    return result;
}

} // namespace helmsman::away
