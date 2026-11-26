// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's side of a planet mission: the pointer and what a click does,
// the action menu, the inventory, and the text boxes.
#include "helmsman/away/mission.hpp"

#include "helmsman/away/menus.hpp"
#include "helmsman/away/ui.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <cstdio>

namespace helmsman::away {

namespace {

namespace st {
constexpr int kNoWalking = 0x1C;
constexpr int kCrewEntering = 0x1D;
constexpr int kRedshirtDead = 0x1E;
constexpr int kSeenTurbolift = 0x29; // the turbolift picture was shown
constexpr int kAction = 0x1F;
constexpr int kActiveObject = 0x20;
constexpr int kPassiveObject = 0x21;
constexpr int kFlags = 0x24;
} // namespace st

constexpr int kInventoryObject = 0x1F;

bool isItem(int o) { return o >= kFirstItem && o < kFirstItem + kItemCount; }


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

void Mission::setPointerPicture(const std::string& name) { setPointer(session_, name); }

Ui Mission::ui() {
    return Ui{session_, scene_, [this] { drawFrame(); }};
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
    if (session_.trace) {
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
    // The speaker first (0: nobody), then the choices up to a 0 or an
    // empty text.
    for (int i = 0; i < 16; ++i) {
        const int p = rdf16(listOffset + i * 2);
        std::string text = p == 0 ? std::string() : rdfText(p, 1000);
        if (i > 0 && text.empty()) {
            break;
        }
        lines.push_back(std::move(text));
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
    Ui ui = this->ui();
    return textBox(ui, lines, x, y, colour);
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

// --- The options -----------------------------------------------------------

namespace {

// The order of the missions, for the descriptions of saved games.

} // namespace

int Mission::buttonMenu(const std::string& file, int x, int y, std::uint32_t hidden) {
    Ui ui = this->ui();
    return away::buttonMenu(ui, file, x, y, hidden);
}

void Mission::optionsMenu() {
    // The menu leaves out the choice that is already in effect.
    std::uint32_t hidden = 0;
    hidden |= 1u << ((session_.musicOn() ? 2 : 3) + 1);
    hidden |= 1u << ((session_.effectsOn ? 4 : 5) + 1);
    const int choice = buttonMenu("options", 0x32, 0x32, hidden);
    switch (choice) {
    case 0:
        saveMenu();
        break;
    case 1:
        loadMenu();
        break;
    case 2:
        session_.setMusicOn(true);
        break;
    case 3:
        session_.setMusicOn(false);
        break;
    case 4:
        session_.effectsOn = true;
        break;
    case 5:
        session_.effectsOn = false;
        session_.stopEffects();
        break;
    case 6:
        quitMenu();
        break;
    case 7:
        textDisplayMenu();
        break;
    default:
        break;
    }
}

void Mission::saveMenu() {
    SaveWriter w;
    w.text("away");
    session_.save(w);
    w.u8(session_.saveVoyage ? 1 : 0);
    if (session_.saveVoyage) {
        session_.saveVoyage(w);
    }
    save(w);
    Ui u = ui();
    saveGameMenu(u, name_, w.data);
}

void Mission::loadMenu() {
    Ui u = ui();
    const int number = loadGameMenu(u);
    if (number >= 0) {
        MissionResult r;
        r.loadSave = number;
        result_ = r;
    }
}

void Mission::quitMenu() {
    const int choice = showText({"Quit Game", "#GENE\\GENER028#Yes, quit the game.", "#GENE\\GENER008#No, do not quit the game."},
                                0x14, 0x14, 0xB0, false);
    if (choice == 0) {
        MissionResult r;
        r.quit = true;
        result_ = r;
    }
}

void Mission::textDisplayMenu() {
    // The choices in the order the box shows them.
    static constexpr int kModes[3] = {1, 0, 2};
    int start = 0;
    for (int i = 0; i < 3; ++i) {
        if (kModes[i] == session_.textMode) {
            start = i;
        }
    }
    std::vector<std::string> lines = {"Text display", "Text subtitles.", "Display text until you press enter.", "No text displayed."};
    // The current choice first, then the others in turn.
    std::vector<std::string> shown = {lines[0]};
    for (int i = 0; i < 3; ++i) {
        shown.push_back(lines[static_cast<std::size_t>(1 + (start + i) % 3)]);
    }
    const int choice = showText(shown, 0x14, 0x1E, 0xB0, false);
    if (choice >= 0 && choice < 3) {
        session_.textMode = kModes[(start + choice) % 3];
    }
}

// The turbolift's map of the ship (the last mission): the player points at
// a deck and the lift goes there. `deck` is 0 in the lift of the upper decks,
// 1 in the lower one. The first time a picture of the lift is shown first.
void Mission::turbolift(int deck) {
    Platform& p = session_.platform;
    std::shared_ptr<const Bitmap> map;
    std::array<std::shared_ptr<const Bitmap>, 6> labels{};
    try {
        map = session_.assets.picture("veng9");
        for (std::size_t i = 0; i < labels.size(); ++i) {
            labels[i] = session_.assets.picture("turbo" + std::to_string(i + 1));
        }
    } catch (const FormatError&) {
        return;
    }
    if (var(st::kSeenTurbolift) == 0) {
        std::shared_ptr<const Bitmap> lift;
        try {
            lift = session_.assets.picture("veng9b");
        } catch (const FormatError&) {
        }
        p.takeClicks();
        p.keys.clear();
        for (int t = 0; lift && t < 45 && !p.quit; ++t) {
            lift->copyTo(p.screen, 0, 0);
            session_.waitTick();
            if (p.takeClicks() != 0) {
                break;
            }
            bool done = false;
            for (const KeyPress& k : p.keys) {
                done = done || k.scancode == 0x01 || k.scancode == 0x1C || k.scancode == 0x39;
            }
            p.keys.clear();
            if (done) {
                break;
            }
        }
        var(st::kSeenTurbolift) = 1;
    }

    // Each deck's name on the map, and where it leads (a room and the
    // door the crew comes out of), or why it cannot be reached.
    struct Place {
        int left, top, right, bottom;
        int onlyFrom; // the lift it can be reached from, or -1 for both
        int room, door;
    };
    constexpr int kBlocked = -2;
    const Place places[] = {
        {0x7F, 0x78, 0x91, 0x7B, 0, 0, 1},
        {0x6E, 0x83, 0x7E, 0x87, 0, 1, 1},
        {0x95, 0x8F, 0xAD, 0x93, -1, 3, deck == 0 ? 1 : 0},
        {0xEF, 0x98, 0xFD, 0xA0, 1, 5, 1},
        {0x6B, 0xA3, 0x80, 0xA7, 1, 7, 1},
        {0x6E, 0xAB, 0x88, 0xAF, -1, kBlocked, 0},
    };
    const auto under = [&]() -> int {
        for (std::size_t i = 0; i < std::size(places); ++i) {
            const Place& pl = places[i];
            if (p.mouseX >= pl.left && p.mouseX <= pl.right && p.mouseY >= pl.top && p.mouseY <= pl.bottom) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    int shown = -1;
    const auto draw = [&] {
        map->copyTo(p.screen, 0, 0);
        if (shown >= 0) {
            labels[static_cast<std::size_t>(shown)]->draw(p.screen, 3, 0xA8);
        }
    };
    Ui u{session_, scene_, draw};
    p.takeClicks();
    p.keys.clear();
    while (!p.quit) {
        // The pointer stays on the lower part of the screen, where the map is.
        if (p.mouseY < 0x60) {
            p.warpPointer = true;
            p.warpX = p.mouseX;
            p.warpY = 0x60;
            p.mouseY = 0x60;
        }
        shown = under();
        draw();
        session_.waitTick();
        bool chose = (p.takeClicks() & 1) != 0;
        for (const KeyPress& k : p.keys) {
            chose = chose || k.scancode == 0x1C || k.scancode == 0x3B;
        }
        p.keys.clear();
        const int at = under();
        if (!chose || at < 0) {
            continue;
        }
        const Place& pl = places[static_cast<std::size_t>(at)];
        if (pl.room == kBlocked) {
            textBox(u, {"", "#GENE\\GENE_F14#Turbolift access is blocked by an extremely high radiation level."}, 0x32, 0x32, 0xB0);
        } else if (pl.onlyFrom >= 0 && pl.onlyFrom != deck) {
            textBox(u, {"", "#GENE\\GENE_F15#This Turbolift cannot reach that area of the ship."}, 0x32, 0x32, 0xB0);
        } else {
            pendingChange_ = RoomChange{pl.room, pl.door % 6};
            return;
        }
    }
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
