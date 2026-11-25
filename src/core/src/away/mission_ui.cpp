// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's side of a planet mission: the pointer and what a click does,
// the action menu, the inventory, and the text boxes.
#include "helmsman/away/mission.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cstdio>

namespace helmsman::away {

namespace {

namespace st {
constexpr int kNoWalking = 0x1C;
constexpr int kCrewEntering = 0x1D;
constexpr int kRedshirtDead = 0x1E;
constexpr int kAction = 0x1F;
constexpr int kActiveObject = 0x20;
constexpr int kPassiveObject = 0x21;
constexpr int kFlags = 0x24;
} // namespace st

constexpr int kInventoryObject = 0x1F;
constexpr int kTextColumns = 24;

bool isItem(int o) { return o >= kFirstItem && o < kFirstItem + kItemCount; }

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

// --- The pointer ----------------------------------------------------------

void Mission::setCursor(int action, bool highlight) {
    cursorAction_ = action;
    cursorHighlight_ = highlight;
    std::string name = "walk";
    switch (action) {
    case kUse:
        name = highlight ? "useh" : "usen";
        break;
    case kGet:
        name = highlight ? "geth" : "getn";
        break;
    case kLook:
        name = highlight ? "lookh" + std::to_string(randomLookFrame_) : "lookn";
        break;
    case kTalk:
        name = highlight ? (talkFrame_ ? "talkh1" : "talkh0") : "talkn";
        break;
    case kOptions:
        name = "options";
        break;
    default:
        name = "walk";
        break;
    }
    try {
        cursor_ = session_.assets.picture(name);
    } catch (const FormatError&) {
        cursor_.reset();
    }
    Platform& p = session_.platform;
    if (cursor_) {
        p.pointer = cursor_->pixels;
        p.pointerHotX = cursor_->handleX;
        p.pointerHotY = cursor_->handleY;
    }
}

void Mission::setPointerPicture(const std::string& name) {
    try {
        const auto bitmap = session_.assets.picture(name);
        Platform& p = session_.platform;
        p.pointer = bitmap->pixels;
        p.pointerHotX = bitmap->handleX;
        p.pointerHotY = bitmap->handleY;
    } catch (const FormatError&) {
    }
}

void Mission::updateCursorHighlight() {
    const int action = var(st::kAction);
    if (action == kWalk || var(st::kCrewEntering) != 0) {
        if (cursorAction_ != kWalk || cursorHighlight_) {
            setCursor(kWalk, false);
        }
        return;
    }
    const Platform& p = session_.platform;
    const int object = objectAt(p.mouseX, p.mouseY);
    bool highlight = false;
    if (object >= 0) {
        // The crew can not be looked at with "use" by themselves; anything
        // else under the pointer lights it.
        highlight = !(object <= 3 && action == kTalk) || object > 0;
    }
    if (action == kLook && highlight && !cursorHighlight_) {
        randomLookFrame_ = static_cast<int>(session_.ticks() % 8);
    }
    if (action == kTalk && highlight && (session_.ticks() & 3) == 0) {
        talkFrame_ = !talkFrame_;
        setCursor(action, true);
        return;
    }
    if (action != cursorAction_ || highlight != cursorHighlight_) {
        setCursor(action, highlight);
    }
}

// --- What is under the pointer --------------------------------------------

bool Mission::insidePolygon(int p, int x, int y) const {
    const int count = rdf16(p + 2);
    if (count < 1 || count > 64) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        const int ax = static_cast<std::int16_t>(rdf16(p + 4 + i * 4));
        const int ay = static_cast<std::int16_t>(rdf16(p + 6 + i * 4));
        const int bx = static_cast<std::int16_t>(rdf16(p + 4 + j * 4));
        const int by = static_cast<std::int16_t>(rdf16(p + 6 + j * 4));
        // Inside lies on the same side of every edge.
        if (static_cast<std::int16_t>((bx - ax) * (y - ay) - (by - ay) * (x - ax)) < 0) {
            return false;
        }
    }
    return true;
}

int Mission::hotspotAt(int x, int y, bool) {
    objectHasWalkTo_ = false;
    const int action = std::max<int>(1, var(st::kAction));
    const int mask = 1 << (action - 1);
    int guard = 0;
    for (int p = rdf16(0x12); p != 0 && p < rdf16(0x14) && guard < 512; ++guard) {
        const int head = rdf16(p);
        if ((head & 0x8000) != 0) {
            const int poly = p + 6;
            if ((head & mask) != 0 && insidePolygon(poly, x, y)) {
                objectHasWalkTo_ = true;
                walkToX_ = static_cast<std::int16_t>(rdf16(p + 2));
                walkToY_ = static_cast<std::int16_t>(rdf16(p + 4));
                return rdf16(poly);
            }
            p = poly + 4 + rdf16(poly + 2) * 4;
            continue;
        }
        if (insidePolygon(p, x, y)) {
            return head;
        }
        p += 4 + rdf16(p + 2) * 4;
    }
    return -1;
}

int Mission::objectAt(int x, int y) {
    std::vector<const Sprite*> sprites;
    for (const auto& a : actors_) {
        if (a.used && a.sprite.bitmap) {
            sprites.push_back(&a.sprite);
        }
    }
    if (selectedIconShown_) {
        sprites.push_back(&selectedIcon_);
    }
    if (inventoryIconShown_) {
        sprites.push_back(&inventoryIcon_);
    }
    sortForDrawing(sprites);
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const Sprite* s = *it;
        if (!s->hit(x, y)) {
            continue;
        }
        if (s == &inventoryIcon_) {
            return kInventoryObject;
        }
        if (s == &selectedIcon_) {
            return var(st::kActiveObject);
        }
        for (int i = 0; i < kActors; ++i) {
            if (&actors_[static_cast<std::size_t>(i)].sprite == s) {
                if (hideCrewFromPointer_ && i <= 3) {
                    break;
                }
                return i;
            }
        }
    }
    return hotspotAt(x, y, true);
}

bool Mission::objectUnavailable(int object, int action) {
    if (action == kLook) {
        return false;
    }
    if (object == 3 && var(st::kRedshirtDead) != 0) {
        return true;
    }
    const int flags = var(st::kFlags);
    if (object >= 0 && object <= 3 && (flags & (1 << object)) != 0) {
        return true;
    }
    if (object == 0x45 && (flags & 4) != 0) {
        return true;
    }
    if (object == 0x44 && (flags & 2) != 0) {
        return true;
    }
    return false;
}

bool Mission::walkToObjectFirst() {
    if (!objectHasWalkTo_) {
        return false;
    }
    objectHasWalkTo_ = false;
    const int action = var(st::kAction);
    const int active = var(st::kActiveObject);
    int who = 0;
    if (action == kUse && active <= 3) {
        who = active;
    } else if (action == kUse && active == 0x44) {
        who = 1;
    } else if (action == kUse && active == 0x45) {
        who = 2;
    }
    int finishEvent = 0;
    if (action != kWalk) {
        Event e{static_cast<std::uint8_t>(action), static_cast<std::uint8_t>(active), 0, 0};
        if (action == kUse) {
            e.b2 = var(st::kPassiveObject);
        }
        if (!hasAction(e)) {
            return false;
        }
        for (int i = 0; i < 31; ++i) {
            Queued& q = queued_[static_cast<std::size_t>(i)];
            if (!q.used) {
                q.used = true;
                q.event = e;
                finishEvent = 0xE0 + i;
                break;
            }
        }
        if (finishEvent == 0) {
            return false;
        }
    }
    Actor& a = actors_[static_cast<std::size_t>(who)];
    walkActor(who, crewName(who, "walk"), a.x, a.y, walkToX_, walkToY_);
    if (finishEvent != 0) {
        a.notifyFinish = true;
        a.finishEvent = finishEvent;
    }
    return true;
}

void Mission::checkTouches() {
    const int x = actors_[0].x;
    const int y = actors_[0].y;
    int guard = 0;
    for (int p = rdf16(0x1A); p != 0 && p < rdf16(0x1C) && guard < 256; ++guard) {
        if (insidePolygon(p, x, y)) {
            const int id = rdf16(p);
            if (id != lastTouched_) {
                lastTouched_ = id;
                post(kTouchedHotspot, static_cast<std::uint8_t>(id & 0xFF));
            }
            return;
        }
        p += 4 + rdf16(p + 2) * 4;
    }
    lastTouched_ = -1;
    if (var(st::kFlags) != 0 || !warpsEnabled_) {
        return;
    }
    guard = 0;
    for (int p = rdf16(0x16); p != 0 && p < rdf16(0x18) && guard < 256; ++guard) {
        if (insidePolygon(p, x, y)) {
            const int id = rdf16(p);
            if (id != lastWarp_) {
                lastWarp_ = id;
                post(kTouchedWarp, static_cast<std::uint8_t>(id & 0xFF));
            }
            return;
        }
        p += 4 + rdf16(p + 2) * 4;
    }
    lastWarp_ = -1;
}

// --- Clicks and keys ---------------------------------------------------------

void Mission::showSelectedIcons() {
    // While "use" or "look" waits for a target: the active thing at the top
    // left, and the inventory beside it.
    clearSelectedIcons();
    const int active = var(st::kAction) == kUse ? var(st::kActiveObject) : -1;
    std::string picture;
    if (active == 0) {
        picture = "ikirk";
    } else if (active == 1) {
        picture = "ispock";
    } else if (active == 2) {
        picture = "imccoy";
    } else if (active == 3) {
        picture = "iredshir";
    } else if (isItem(active)) {
        picture = itemInfo(active).picture;
    }
    int x = 10;
    if (!picture.empty()) {
        try {
            selectedIcon_.bitmap = session_.assets.picture(picture);
            selectedIcon_.mode = SpriteMode::Overlay;
            selectedIcon_.x = 10;
            selectedIcon_.y = 10;
            selectedIcon_.depth = 15;
            selectedIcon_.order = 8;
            selectedIconShown_ = true;
            x = 0x2E;
        } catch (const FormatError&) {
        }
    }
    try {
        inventoryIcon_.bitmap = session_.assets.picture("inv00");
        inventoryIcon_.mode = SpriteMode::Overlay;
        inventoryIcon_.x = x;
        inventoryIcon_.y = 10;
        inventoryIcon_.depth = 15;
        inventoryIcon_.order = 8;
        inventoryIconShown_ = true;
    } catch (const FormatError&) {
    }
    refreshExtraSprites();
}

void Mission::clearSelectedIcons() {
    selectedIconShown_ = false;
    inventoryIconShown_ = false;
    refreshExtraSprites();
}

void Mission::refreshExtraSprites() {
    extraSprites_.clear();
    if (selectedIconShown_) {
        extraSprites_.push_back(&selectedIcon_);
    }
    if (inventoryIconShown_) {
        extraSprites_.push_back(&inventoryIcon_);
    }
}

void Mission::handleInput() {
    Platform& p = session_.platform;
    const std::uint8_t clicks = p.takeClicks();
    if ((clicks & 1) != 0) {
        leftClick();
        return;
    }
    if ((clicks & 2) != 0) {
        rightClick();
        return;
    }
    while (!p.keys.empty()) {
        const KeyPress key = p.keys.front();
        p.keys.pop_front();
        if (var(st::kCrewEntering) != 0) {
            continue;
        }
        switch (key.scancode) {
        case scancode::kEnter:
        case scancode::kF1:
            leftClick();
            return;
        case scancode::kEscape:
        case scancode::kSpace:
        case 0x3C: // F2
            rightClick();
            return;
        case 0x11: // W
            clearSelectedIcons();
            selectAction(kWalk);
            return;
        case 0x14: // T
            clearSelectedIcons();
            selectAction(kTalk);
            return;
        case 0x16: // U
            clearSelectedIcons();
            selectAction(kUse);
            return;
        case 0x22: // G
            clearSelectedIcons();
            selectAction(kGet);
            return;
        case 0x26: // L
            clearSelectedIcons();
            selectAction(kLook);
            return;
        case 0x17: // I
            if (var(st::kAction) == kUse || var(st::kAction) == kLook) {
                clearSelectedIcons();
                const int item = inventory(var(st::kAction) == kUse);
                if (item >= 0) {
                    targetChosen(item);
                }
            }
            return;
        default:
            break;
        }
    }
}

void Mission::rightClick() {
    if (var(st::kCrewEntering) != 0) {
        return;
    }
    clearSelectedIcons();
    const int action = actionMenu();
    if (action == kOptions) {
        optionsMenu();
        selectAction(kWalk);
        return;
    }
    selectAction(action);
}

void Mission::selectAction(int action) {
    var(st::kAction) = static_cast<std::uint8_t>(action);
    setCursor(action, false);
    if (action == kUse) {
        // First the thing used, then what it is used on.
        const int active = chooseUseTarget();
        if (active < 0) {
            var(st::kAction) = kWalk;
            setCursor(kWalk, false);
            clearSelectedIcons();
            return;
        }
        if (active == 0x100) {
            // Kirk used something right away.
            clearSelectedIcons();
            return;
        }
        var(st::kActiveObject) = static_cast<std::uint8_t>(active);
        if (active == 0x47 && (var(st::kFlags) & 1) == 0) {
            // The communicator works on its own.
            post(kUse, 0x47, 0, 0);
            session_.playEffect("COMMUNIC");
            var(st::kAction) = kWalk;
            setCursor(kWalk, false);
            return;
        }
        if ((var(st::kFlags) & 1) == 0) {
            showSelectedIcons();
        }
        return;
    }
    if (action == kLook && (var(st::kFlags) & 1) == 0) {
        showSelectedIcons();
    }
}

int Mission::chooseUseTarget() {
    // Pick with a click what is used: a crewman, the inventory or a thing
    // in the room. Right click or Escape gives up.
    Platform& p = session_.platform;
    if ((var(st::kFlags) & 1) == 0) {
        showSelectedIcons();
    }
    while (true) {
        session_.waitTick();
        tickOnce();
        drawFrame();
        processEvents();
        if (pendingChange_ || result_) {
            return -1;
        }
        const std::uint8_t clicks = p.takeClicks();
        int object = -2;
        bool inventoryKey = false;
        while (!p.keys.empty()) {
            const KeyPress key = p.keys.front();
            p.keys.pop_front();
            if (key.scancode == scancode::kEscape || key.scancode == scancode::kSpace) {
                clearSelectedIcons();
                return -1;
            }
            if (key.scancode == 0x17) {
                inventoryKey = true;
            }
            if (key.scancode == scancode::kEnter) {
                object = objectAt(p.mouseX, p.mouseY);
            }
        }
        if ((clicks & 2) != 0) {
            clearSelectedIcons();
            return -1;
        }
        if (inventoryKey) {
            clearSelectedIcons();
            const int item = inventory(true);
            if (item >= 0) {
                return item;
            }
            showSelectedIcons();
            continue;
        }
        if ((clicks & 1) != 0) {
            object = objectAt(p.mouseX, p.mouseY);
        }
        if (object == -2) {
            continue;
        }
        if (object == -1) {
            continue;
        }
        if (objectUnavailable(object, kUse)) {
            continue;
        }
        if (object == kInventoryObject) {
            clearSelectedIcons();
            const int item = inventory(false);
            if (item >= 0) {
                return item;
            }
            showSelectedIcons();
            continue;
        }
        if (object > 3) {
            // A thing in the room: Kirk uses it, when the room knows how.
            if (objectUnavailable(0, kUse)) {
                continue;
            }
            if (hasAction(Event{kUse, 0, static_cast<std::uint8_t>(object), 0}) ||
                hasAction(Event{kGet, static_cast<std::uint8_t>(object), 0, 0}) ||
                hasAction(Event{kWalk, static_cast<std::uint8_t>(object), 0, 0})) {
                var(st::kActiveObject) = 0;
                var(st::kPassiveObject) = static_cast<std::uint8_t>(object);
                var(st::kAction) = kUse;
                if (!walkToObjectFirst()) {
                    post(kUse, 0, static_cast<std::uint8_t>(object), 0);
                }
                return 0x100;
            }
            continue;
        }
        return object;
    }
}

void Mission::targetChosen(int object) {
    const int action = var(st::kAction);
    if (action == kUse) {
        var(st::kPassiveObject) = static_cast<std::uint8_t>(object);
        if (!walkToObjectFirst()) {
            post(kUse, var(st::kActiveObject), static_cast<std::uint8_t>(object), 0);
        }
        clearSelectedIcons();
        if ((var(st::kFlags) & 1) == 0) {
            // "Use" stays: show the icons again for the next target.
        }
        return;
    }
    var(st::kActiveObject) = static_cast<std::uint8_t>(object);
    if (!walkToObjectFirst()) {
        post(static_cast<std::uint8_t>(action), static_cast<std::uint8_t>(object), 0, 0);
    }
    if (action == kLook && (var(st::kFlags) & 1) == 0) {
        showSelectedIcons();
    }
}

void Mission::leftClick() {
    if (var(st::kCrewEntering) != 0) {
        return;
    }
    Platform& p = session_.platform;
    const int action = var(st::kAction);
    if (action == kWalk) {
        if (var(st::kNoWalking) != 0) {
            return;
        }
        hideCrewFromPointer_ = true;
        const int object = objectAt(p.mouseX, p.mouseY);
        hideCrewFromPointer_ = false;
        if (walkToObjectFirst()) {
            return;
        }
        if (object > 0 && object < 0x40) {
            post(kWalk, static_cast<std::uint8_t>(object));
        } else {
            const Actor& kirk = actors_[0];
            walkActor(0, crewName(0, "walk"), kirk.x, kirk.y, p.mouseX, p.mouseY);
        }
        return;
    }
    int object = objectAt(p.mouseX, p.mouseY);
    if (trace_) {
        std::fprintf(stderr, "[%llu] click %d at %d,%d on %d\n", static_cast<unsigned long long>(p.millis / 16), action, p.mouseX, p.mouseY, object);
    }
    if (objectUnavailable(object, action)) {
        return;
    }
    if (object == kInventoryObject) {
        clearSelectedIcons();
        object = inventory(action == kUse);
        if (object < 0) {
            if ((var(st::kFlags) & 1) == 0) {
                showSelectedIcons();
            }
            return;
        }
        objectHasWalkTo_ = false;
    }
    if (object < 0) {
        return;
    }
    if (action == kUse && object == var(st::kActiveObject)) {
        return;
    }
    targetChosen(object);
}

// --- Text boxes ---------------------------------------------------------------

int Mission::showTextList(std::uint16_t listOffset) {
    std::vector<std::string> lines;
    // The speaker first (0: nobody), then the choices up to a 0.
    for (int i = 0; i < 16; ++i) {
        const int p = rdf16(listOffset + i * 2);
        if (p == 0 && i > 0) {
            break;
        }
        lines.push_back(p == 0 ? std::string() : rdfText(p, 1000));
    }
    if (lines.empty()) {
        return 0;
    }
    const std::string& speaker = lines[0];
    std::uint8_t colour = 0x88;
    if (speaker.empty() || speaker == "Capt. Kirk") {
        colour = 0xB0;
    } else if (speaker == "Mr. Spock" || speaker == "Dr. McCoy") {
        colour = 0xC0;
    } else if (speaker == "Mr. Chekov") {
        colour = 0xB0;
    } else if (speaker == "Mr. Scott" || speaker.rfind("Lt", 0) == 0 || speaker.rfind("Ensign", 0) == 0) {
        colour = 0xA1;
    }
    return showText(lines, 0x14, 0x14, colour, true);
}

void Mission::showDefault(const std::string& speaker, int textIndex, std::uint8_t colour) {
    showText({speaker, session_.textLine("ground", textIndex)}, 0x14, 0x14, colour, false);
}

int Mission::showText(const std::vector<std::string>& lines, int x, int y, std::uint8_t colour, bool) {
    if (lines.size() < 2) {
        return 0;
    }
    Platform& p = session_.platform;
    const std::string& speaker = lines[0];
    const int choices = static_cast<int>(lines.size()) - 1;
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
    if (const auto menu = session_.archive.load("TEXTBTNS.MNU")) {
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
                b.bitmap = session_.assets.picture(b.picture);
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
    setPointerPicture("pushbtn");
    p.warpPointer = true;
    p.warpX = left + 0xDE;
    p.warpY = top - 8;
    p.takeClicks();
    p.keys.clear();

    bool done = false;
    while (!done) {
        if (shownChoice != choice) {
            shownChoice = choice;
            const auto& spoken = texts[static_cast<std::size_t>(choice)];
            if (!spoken.voice.empty()) {
                session_.playSpeech(spoken.voice);
            }
        }
        fill();
        drawFrame();
        scene_.drawSprite(p.screen, box);
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
        session_.waitTick();
        int pressed = -1;
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
    session_.stopSpeech();
    p.pointer = savedPointer;
    p.pointerHotX = savedHotX;
    p.pointerHotY = savedHotY;
    p.pointerTargets.clear();
    p.takeClicks();
    return choice;
}

// --- The action menu ------------------------------------------------------------

int Mission::actionMenu() {
    Platform& p = session_.platform;
    std::shared_ptr<const Bitmap> picture;
    try {
        picture = session_.assets.picture("action");
    } catch (const FormatError&) {
        return kWalk;
    }
    constexpr int kLeft = 50;
    constexpr int kTop = 50;
    const int savedX = p.mouseX;
    const int savedY = p.mouseY;
    p.warpPointer = true;
    p.warpX = kLeft + picture->width() / 2;
    p.warpY = kTop + picture->height() / 2;

    Sprite menu;
    menu.bitmap = picture;
    menu.x = kLeft;
    menu.y = kTop;
    menu.mode = SpriteMode::Overlay;
    menu.depth = 15;

    struct Choice {
        int action;
        int left, top, right, bottom; // inclusive, from the menu's corner
        int pointX, pointY;
        const char* pointer;
    };
    static constexpr Choice kChoices[] = {
        {kOptions, 39, 2, 50, 17, 0x2C, 2, "options"}, {kLook, 18, 2, 38, 9, 0x1C, 6, "look"},
        {kTalk, 18, 11, 38, 17, 0x1B, 14, "talk"},     {kUse, 2, 16, 13, 26, 7, 19, "use"},
        {kGet, 40, 34, 53, 43, 0x2C, 0x26, "get"},
    };
    int selected = kWalk;
    p.takeClicks();
    p.keys.clear();
    p.pointerTargets.clear();
    for (const auto& c : kChoices) {
        p.pointerTargets.push_back(ClipRect{kLeft + c.left, kTop + c.top, kLeft + c.right + 1, kTop + c.bottom + 1});
    }
    bool done = false;
    bool pointerMoved = true;
    int lastX = -1;
    int lastY = -1;
    while (!done) {
        if (p.mouseX != lastX || p.mouseY != lastY || pointerMoved) {
            pointerMoved = false;
            lastX = p.mouseX;
            lastY = p.mouseY;
            selected = kWalk;
            const int rx = p.mouseX - kLeft;
            const int ry = p.mouseY - kTop;
            for (const auto& c : kChoices) {
                if (rx >= c.left && rx <= c.right && ry >= c.top && ry <= c.bottom) {
                    selected = c.action;
                }
            }
            setPointerPicture(selected == kWalk ? "walk" : [&] {
                for (const auto& c : kChoices) {
                    if (c.action == selected) {
                        return c.pointer;
                    }
                }
                return "walk";
            }());
        }
        drawFrame();
        scene_.drawSprite(p.screen, menu);
        session_.waitTick();
        if (p.takeClicks() != 0) {
            done = true;
        }
        while (!p.keys.empty()) {
            const KeyPress key = p.keys.front();
            p.keys.pop_front();
            switch (key.scancode) {
            case scancode::kEnter:
            case scancode::kF1:
            case scancode::kEscape:
            case scancode::kSpace:
            case 0x3C:
                done = true;
                break;
            case 0x11: selected = kWalk; done = true; break;
            case 0x14: selected = kTalk; done = true; break;
            case 0x16: selected = kUse; done = true; break;
            case 0x22: selected = kGet; done = true; break;
            case 0x26: selected = kLook; done = true; break;
            case 0x17: selected = kUse; done = true; break;
            default:
                break;
            }
        }
        if (p.quit) {
            break;
        }
    }
    session_.playEffect("BUTTON1");
    p.pointerTargets.clear();
    // The pointer goes back where it was when it ends on the menu.
    if (p.mouseX >= kLeft && p.mouseX < kLeft + picture->width() && p.mouseY >= kTop && p.mouseY < kTop + picture->height()) {
        p.warpPointer = true;
        p.warpX = savedX;
        p.warpY = savedY;
    }
    return selected;
}

// --- The inventory ----------------------------------------------------------------

int Mission::inventory(bool) {
    Platform& p = session_.platform;
    struct Entry {
        int item;
        Sprite sprite;
    };
    std::vector<Entry> entries;
    int n = 0;
    for (int i = 0; i < kItemCount; ++i) {
        if (!session_.carried[static_cast<std::size_t>(i)]) {
            continue;
        }
        Entry e;
        e.item = kFirstItem + i;
        try {
            e.sprite.bitmap = session_.assets.picture(itemInfo(e.item).picture);
        } catch (const FormatError&) {
            continue;
        }
        e.sprite.mode = SpriteMode::Overlay;
        e.sprite.x = 0x32 + (n % 5) * 32;
        e.sprite.y = 0x32 + (n / 5) * 32;
        e.sprite.depth = 15;
        e.sprite.order = 8;
        entries.push_back(std::move(e));
        ++n;
    }
    if (entries.empty()) {
        return -1;
    }
    const auto savedPointer = p.pointer;
    const int savedHotX = p.pointerHotX;
    const int savedHotY = p.pointerHotY;
    setPointerPicture("walk");
    p.takeClicks();
    p.keys.clear();
    p.pointerTargets.clear();
    for (const auto& e : entries) {
        int l = 0, t = 0, w = 0, h = 0;
        if (e.sprite.bounds(l, t, w, h)) {
            p.pointerTargets.push_back(ClipRect{l, t, l + w, t + h});
        }
    }
    p.warpPointer = true;
    p.warpX = entries[0].sprite.x + 16;
    p.warpY = entries[0].sprite.y + 16;
    int result = -1;
    bool done = false;
    while (!done) {
        drawFrame();
        for (const auto& e : entries) {
            scene_.drawSprite(p.screen, e.sprite);
        }
        session_.waitTick();
        const std::uint8_t clicks = p.takeClicks();
        if ((clicks & 1) != 0) {
            for (const auto& e : entries) {
                if (e.sprite.hit(p.mouseX, p.mouseY)) {
                    result = e.item;
                }
            }
            done = true;
        } else if ((clicks & 2) != 0) {
            done = true;
        }
        while (!p.keys.empty()) {
            const KeyPress key = p.keys.front();
            p.keys.pop_front();
            if (key.scancode == scancode::kEnter || key.scancode == scancode::kF1) {
                for (const auto& e : entries) {
                    if (e.sprite.hit(p.mouseX, p.mouseY)) {
                        result = e.item;
                    }
                }
                done = true;
            } else if (key.scancode == scancode::kEscape || key.scancode == scancode::kSpace) {
                done = true;
            }
        }
        if (p.quit) {
            break;
        }
    }
    session_.playEffect("BUTTON1");
    p.pointer = savedPointer;
    p.pointerHotX = savedHotX;
    p.pointerHotY = savedHotY;
    p.pointerTargets.clear();
    return result;
}

void Mission::optionsMenu() {
    // Helmsman's own menu comes with the game's options.
}

void Mission::showPicture(const std::string& name) {
    Platform& p = session_.platform;
    std::shared_ptr<const Bitmap> picture;
    try {
        picture = session_.assets.picture(name);
    } catch (const FormatError&) {
        return;
    }
    p.takeClicks();
    p.keys.clear();
    while (!p.takeSkip()) {
        drawFrame();
        picture->draw(p.screen, 0, 0);
        session_.waitTick();
        if (p.quit) {
            break;
        }
    }
}

} // namespace helmsman::away
