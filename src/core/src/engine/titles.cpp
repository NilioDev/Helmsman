// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/titles.hpp"

#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"
#include "helmsman/space/space.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>

namespace helmsman {

namespace {

using space::Kind;
using space::Object;
using space::Vec;

// A thing that moves on its own a little every frame, as the titles place
// it: a point on a circle around the watcher, heading for another.
struct Mover {
    int index = -1;
    std::int32_t dx = 0;
    std::int32_t dz = 0;
};

class Titles {
public:
    explicit Titles(away::Session& session) : session_(session), space_(session.archive) {}

    void play();

private:
    bool showLogo();
    void loadPalette(const std::string& name);
    void place(Mover& m, int fromAngle, int fromDistance, int toAngle, int toDistance, int frames);
    void addShip();
    void addStill(const std::string& picture, int scale);
    void words(int n); // the opening words, -1 for none
    void event(int frame);
    bool waitFrame();

    away::Session& session_;
    space::Space space_;
    Vec camera_{};
    std::int32_t cameraSpeed_ = 0;
    Mover ship_;
    Mover still_;
    std::shared_ptr<const Bitmap> words_;
    std::uint64_t nextFrame_ = 0;
};

constexpr int kFrames = 0x186;
constexpr std::uint64_t kFrameMillis = 100;

void Titles::loadPalette(const std::string& name) {
    if (const auto pal = session_.archive.load(name + ".PAL")) {
        try {
            session_.platform.palette = Palette::fromBytes(*pal);
        } catch (const FormatError&) {
        }
    }
}

bool Titles::showLogo() {
    Platform& p = session_.platform;
    loadPalette("GOLD");
    p.screen.fill(0);
    if (const auto logo = session_.archive.load("GOLDLOGO.BMP")) {
        try {
            Bitmap::fromBytes(*logo).copyTo(p.screen, 0, 0);
        } catch (const FormatError&) {
        }
    }
    session_.playEffect("LOGO");
    // As long as its sound lasts, at most half a minute.
    const std::uint64_t until = p.millis + 30000;
    p.waitFrame();
    while (!p.quit && p.millis < until) {
        if (p.takeSkip()) {
            return false;
        }
        if (p.soundQueued[static_cast<int>(SoundChannel::Effect)] <= 0.0) {
            break;
        }
        p.waitFrame();
    }
    return !p.quit;
}

void Titles::place(Mover& m, int fromAngle, int fromDistance, int toAngle, int toDistance, int frames) {
    // Angles in degrees, 0 straight ahead, growing to the right.
    auto at = [this](int degrees, int distance) {
        const double a = degrees * std::numbers::pi / 180.0;
        return Vec{camera_.x + static_cast<std::int32_t>(std::lround(std::sin(a) * distance)), 0,
                   camera_.z + static_cast<std::int32_t>(std::lround(std::cos(a) * distance))};
    };
    Object& o = space_.object(m.index);
    o.pos = at(fromAngle, fromDistance);
    const Vec to = at(toAngle, toDistance);
    const std::int32_t dx = to.x - o.pos.x;
    const std::int32_t dz = to.z - o.pos.z;
    o.axes = space::heading(space::angleOf(dx, dz));
    m.dx = frames > 0 ? dx / frames : 0;
    m.dz = frames > 0 ? dz / frames : 0;
}

void Titles::addShip() {
    space_.setPlayer("ENTERPRI");
    ship_.index = space::Space::kPlayer;
}

void Titles::addStill(const std::string& picture, int scale) {
    still_.index = space_.addStill(picture, Vec{});
    if (still_.index >= 0) {
        Object& o = space_.object(still_.index);
        o.scale = scale;
        o.pos = camera_;
    }
}

void Titles::words(int n) {
    words_.reset();
    if (n < 0 || session_.textMode == 2) {
        return;
    }
    char name[32];
    std::snprintf(name, sizeof name, "TITTXT%02d.BMP", n);
    if (const auto b = session_.archive.load(name)) {
        try {
            words_ = std::make_shared<const Bitmap>(Bitmap::fromBytes(*b));
        } catch (const FormatError&) {
        }
    }
}

void Titles::event(int frame) {
    auto removeStill = [this] {
        if (still_.index >= 0) {
            space_.remove(still_.index);
            still_ = Mover{};
        }
    };
    auto credit = [&](int n) {
        removeStill();
        char name[32];
        std::snprintf(name, sizeof name, "credit%02d", n);
        addStill(name, 1);
        if (still_.index >= 0) {
            // Where the title came to rest: at its own size.
            space_.object(still_.index).pos = camera_ + Vec{0, 0, space_.focal()};
        }
    };
    switch (frame) {
    case 0:
        cameraSpeed_ = 10;
        session_.playMusic(0);
        break;
    case 0x1E:
        session_.playEffect("KIRKINTR");
        words(0);
        break;
    case 0x24: words(1); break;
    case 0x2A:
        words(-1);
        addShip();
        place(ship_, 330, 5000, 0, 0, 0x12);
        break;
    case 0x3C: place(ship_, 0, 0, 30, 5000, 6); break;
    case 0x42:
        words(2);
        // Drawn at its own size 10000 away.
        addStill("planet", static_cast<int>(10000 / std::max<std::int32_t>(1, space_.focal())));
        if (still_.index >= 0) {
            place(still_, 6, 10000, 6, 10000, 0);
        }
        place(ship_, -15, 250, 15, 500, 0x12);
        break;
    case 0x5A:
        words(3);
        if (still_.index >= 0) {
            place(still_, 0, 5000, 0, 5000, 0);
            space_.object(still_.index).pos.y = -2500;
        }
        place(ship_, -30, 450, 45, 540, 0x36);
        break;
    case 0x6C: words(4); break;
    case 0x7E: words(5); break;
    case 0x90:
        words(6);
        if (still_.index >= 0) {
            place(still_, 30, 5600, 30, 5600, 0);
            space_.object(still_.index).pos.y = -2500;
        }
        place(ship_, -10, 800, 155, 600, 0x12);
        break;
    case 0xA2:
        cameraSpeed_ = 10;
        removeStill();
        words(-1);
        place(ship_, 340, 5000, 0, 0, 0x12);
        break;
    case 0xBA:
        space_.remove(space::Space::kPlayer);
        ship_ = Mover{};
        addStill("credit00", 1);
        if (still_.index >= 0) {
            place(still_, 0, 0, 0, space_.focal(), 0x12);
        }
        break;
    case 0xCC:
        still_.dz = 0;
        still_.dx = 0;
        break;
    case 0xD8:
        if (const auto b = session_.archive.load("LEGAL.BMP")) {
            try {
                words_ = std::make_shared<const Bitmap>(Bitmap::fromBytes(*b));
            } catch (const FormatError&) {
            }
        }
        break;
    case 0xDE:
        words_.reset();
        removeStill();
        break;
    case 0x16E: removeStill(); break;
    default:
        // The names: a new card every twelve frames, then every six.
        if (frame >= 0xE4 && frame < 0x144 && (frame - 0xE4) % 12 == 0) {
            credit(1 + (frame - 0xE4) / 12);
        } else if (frame >= 0x144 && frame <= 0x168 && (frame - 0x144) % 6 == 0) {
            credit(9 + (frame - 0x144) / 6);
        }
        break;
    }
}

bool Titles::waitFrame() {
    Platform& p = session_.platform;
    while (!p.quit && p.millis < nextFrame_) {
        p.waitFrame();
        if (p.takeSkip()) {
            return false;
        }
    }
    nextFrame_ += kFrameMillis;
    return !p.quit && !p.takeSkip();
}

void Titles::play() {
    Platform& p = session_.platform;
    p.pointerShown = false;
    p.takeSkip();
    if (!showLogo()) {
        session_.stopEffects();
        p.pointerShown = true;
        return;
    }
    loadPalette("BRIDGE");
    session_.loadMusic("title");
    space_.setView(ClipRect{10, 20, 310, 170}, 4); // 45 degrees to each side
    space_.clear();
    space_.resetStars();
    camera_ = Vec{};
    nextFrame_ = p.millis;
    for (int frame = 0; frame <= kFrames; ++frame) {
        event(frame);
        space_.watchAlongZ(camera_);
        p.screen.fill(0);
        space_.draw(p.screen);
        if (words_) {
            words_->copyTo(p.screen, -words_->handleX, -words_->handleY);
        }
        if (!waitFrame()) {
            break;
        }
        // Everything drifts on.
        camera_.z += cameraSpeed_;
        if (ship_.index >= 0) {
            Object& o = space_.object(ship_.index);
            o.pos.x += ship_.dx;
            o.pos.z += ship_.dz + cameraSpeed_;
        }
        if (still_.index >= 0) {
            Object& o = space_.object(still_.index);
            o.pos.x += still_.dx;
            o.pos.z += still_.dz + cameraSpeed_;
        }
    }
    session_.stopEffects();
    session_.stopMusic();
    p.screen.fill(0);
    p.pointerShown = true;
}

} // namespace

void playOpeningTitles(away::Session& session) {
    Titles t(session);
    t.play();
}

} // namespace helmsman
