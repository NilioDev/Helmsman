// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/bridge/transporter.hpp"

#include "helmsman/away/scene.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"

#include <algorithm>
#include <array>
#include <random>

namespace helmsman::bridge {

namespace {

// An animation that plays once.
struct Player {
    const std::vector<away::AnimFrame>* frames = nullptr;
    int frame = 0;
    std::uint64_t nextTick = 0;
    int x = 0;
    int y = 0;
    bool done = true;
    away::Sprite sprite;
};

void show(away::Session& s, Player& p) {
    const away::AnimFrame& f = (*p.frames)[static_cast<std::size_t>(p.frame)];
    try {
        p.sprite.bitmap = s.assets.picture(f.picture);
    } catch (const FormatError&) {
        p.sprite.bitmap.reset();
    }
    p.sprite.x = p.x + f.x;
    p.sprite.y = p.y + f.y;
    p.sprite.depth = f.depth;
}

void start(away::Session& s, Player& p, const std::string& anim, int x, int y) {
    p = Player{};
    p.frames = &s.assets.anim(anim);
    p.x = x;
    p.y = y;
    p.done = p.frames->empty();
    if (!p.done) {
        show(s, p);
        p.nextTick = s.ticks() + static_cast<std::uint64_t>(p.frames->front().ticks);
    }
}

void step(away::Session& s, Player& p, std::mt19937& rng) {
    if (p.done || s.ticks() < p.nextTick) {
        return;
    }
    const int count = static_cast<int>(p.frames->size());
    const int next = (*p.frames)[static_cast<std::size_t>(std::min(p.frame, count - 1))].next[rng() % 4];
    if (next >= count) {
        p.done = true;
        p.sprite.bitmap.reset();
        return;
    }
    p.frame = next;
    show(s, p);
    p.nextTick = s.ticks() + static_cast<std::uint64_t>(std::max(1, (*p.frames)[static_cast<std::size_t>(p.frame)].ticks));
}

} // namespace

void transporter(away::Session& session, bool down, int crew, const std::string& extra) {
    Platform& platform = session.platform;
    away::Scene scene;
    scene.font = &session.font;
    try {
        platform.palette = Palette::fromBytes(session.archive.need("PALETTE.PAL"));
        scene.backdrop.fill(0);
        Bitmap::fromBytes(session.archive.need("TRANSPRT.BMP")).copyTo(scene.backdrop, 0, 0);
    } catch (const FormatError&) {
        return;
    }
    // Where each of the landing party stands on the pads.
    static constexpr int kPads[4][2] = {{142, 124}, {190, 124}, {126, 114}, {170, 114}};
    static constexpr char kLetters[4] = {'k', 's', 'm', 'r'};
    const std::string how = down ? "teled" : "teleb";
    std::array<Player, 6> players{};
    for (int i = 0; i < std::clamp(crew, 0, 4); ++i) {
        start(session, players[static_cast<std::size_t>(i)], std::string(1, kLetters[i]) + how, kPads[i][0], kPads[i][1]);
    }
    if (!extra.empty()) {
        start(session, players[4], extra, 0x61, 0x79);
    }
    start(session, players[5], "transc", 0, 0);
    platform.pointerShown = false;
    session.playEffect("TRANSENE");
    session.playEffect(down ? "TRANSDEM" : "TRANSMAT");
    std::mt19937 rng{std::random_device{}()};
    int guard = 0;
    while (!platform.quit && guard++ < 18 * 20) {
        bool busy = false;
        for (auto& p : players) {
            step(session, p, rng);
        }
        for (int i = 0; i < 5; ++i) {
            busy = busy || !players[static_cast<std::size_t>(i)].done;
        }
        std::vector<const away::Sprite*> sprites;
        for (const auto& p : players) {
            if (!p.done && p.sprite.bitmap) {
                sprites.push_back(&p.sprite);
            }
        }
        scene.draw(platform.screen, sprites);
        if (!busy) {
            break;
        }
        session.waitTick();
    }
    platform.pointerShown = true;
}

} // namespace helmsman::bridge
