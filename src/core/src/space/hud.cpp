// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/space/hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace helmsman::space {

namespace {

void line(Surface& to, int x, int y, int len, std::uint8_t c) {
    for (int i = 0; i < len; ++i) {
        to.set(x + i, y, c);
    }
}

void panelPicture(Surface& to, const PictureSource& pictures, const std::string& name, bool opaque, int recolour = -1) {
    const auto b = pictures(name);
    if (!b) {
        return;
    }
    const int left = -b->handleX;
    const int top = -b->handleY;
    for (int y = 0; y < b->height(); ++y) {
        for (int x = 0; x < b->width(); ++x) {
            std::uint8_t c = b->pixels.at(x, y);
            if (c == 0 && !opaque) {
                continue;
            }
            if (c != 0 && recolour >= 0) {
                c = static_cast<std::uint8_t>(recolour);
            }
            to.set(left + x, top + y, c);
        }
    }
}

} // namespace

void drawHudBars(Surface& to, const Space& space, const Bitmap* lights, const HudColours& colours) {
    const Object& p = space.object(Space::kPlayer);
    // The weapons: a bar fills as each comes ready again, with a light.
    static constexpr int kWeaponAt[4][2] = {{196, 16}, {194, 20}, {90, 16}, {92, 20}};
    for (int i = 0; i < 4; ++i) {
        const Part& part = p.cls.parts[static_cast<std::size_t>(kPortTorpedoes + i)];
        const Mount& m = p.cls.mounts[static_cast<std::size_t>(i)];
        int fill = 24;
        int light = 0;
        std::uint8_t colour = colours.empty;
        if (!p.weaponsArmed) {
            light = 0;
        } else if (!p.weaponsWork || part.now < part.best / 2) {
            light = 0x18;
        } else {
            colour = colours.weapon[static_cast<std::size_t>(i & 1)];
            if (m.readyAt > space.ticks()) {
                fill = 24 - static_cast<int>((m.readyAt - space.ticks()) * 24 /
                                             static_cast<std::uint64_t>(std::max(1, m.reload)));
                light = 8;
            } else {
                light = 0x10;
            }
        }
        const int x = kWeaponAt[i][0];
        const int y = kWeaponAt[i][1];
        line(to, x, y, fill, colour);
        line(to, x + fill, y, 24 - fill, colours.empty);
        if (lights != nullptr) {
            const int row = x > 0xA0 ? 3 : 0;
            for (int yy = 0; yy < 3; ++yy) {
                for (int xx = 0; xx < 8; ++xx) {
                    if (lights->pixels.contains(light + xx, row + yy)) {
                        to.set(x + 0x1B + xx, y - 1 + yy, lights->pixels.at(light + xx, row + yy));
                    }
                }
            }
        }
    }
    // The warp engines' power.
    static constexpr int kPowerAt[2][2] = {{140, 13}, {140, 16}};
    for (int i = 0; i < 2; ++i) {
        const Part& part = p.cls.parts[static_cast<std::size_t>(kPortWarp + i)];
        const int value = std::min(part.best, part.now + (p.powerBoost > 0 ? 5 : 0));
        const int len = part.best > 0 ? std::clamp(0x22 * value / part.best, 0, 0x22) : 0;
        const int x = kPowerAt[i][0];
        const int y = kPowerAt[i][1];
        for (int row = 0; row < 2; ++row) {
            line(to, x, y + row, len, colours.power);
            line(to, x + len, y + row, 0x22 - len, colours.empty);
            line(to, x + 0x23, y + row, 4, p.powerBoost > 0 ? colours.boost : colours.empty);
        }
    }
    // Speed: asked for, and made.
    const std::int32_t maxSpeed = std::max<std::int32_t>(1, p.cls.maxSpeed);
    const int want = std::clamp(static_cast<int>(static_cast<std::int64_t>(p.wantSpeed) * 25 / maxSpeed), 0, 25);
    line(to, 0x8F, 0x14, want, colours.wantSpeed);
    line(to, 0x8F + want, 0x14, 25 - want, colours.speedEmpty);
    const int made = std::clamp(static_cast<int>(static_cast<std::int64_t>(p.speed) * 24 / maxSpeed), 0, 24);
    line(to, 0x90, 0x16, made, colours.madeSpeed);
    line(to, 0x90 + made, 0x16, 24 - made, colours.speedEmpty);
}

void drawShipPanels(Surface& to, const Object& ship, const PictureSource& pictures,
                    const std::vector<std::string>& names) {
    if (names.empty()) {
        return;
    }
    const std::string& name = names[static_cast<std::size_t>(std::clamp(ship.type, 0, static_cast<int>(names.size()) - 1))];
    panelPicture(to, pictures, "t" + name, true);
    panelPicture(to, pictures, "s" + name, true);
    // Parts below three quarters of their strength light up.
    for (int i = 0; i < 10; ++i) {
        const Part& part = ship.cls.parts[static_cast<std::size_t>(i)];
        if (part.now * 4 < part.best * 3) {
            char n[16];
            std::snprintf(n, sizeof n, "%02d", i);
            panelPicture(to, pictures, "t" + name + n, false);
            panelPicture(to, pictures, "s" + name + n, false);
        }
    }
    // The shields, from bright (strong) to dark (weak or down).
    for (int i = 0; i < 6; ++i) {
        const Part& sh = ship.cls.shields[static_cast<std::size_t>(i)];
        const int colour = ship.shieldsWork ? 0xB0 + (sh.best - sh.now) * 8 / (sh.best + 1) : 0xB7;
        char n[24];
        std::snprintf(n, sizeof n, "%sshd%02d", name.c_str(), i);
        panelPicture(to, pictures, n, false, colour);
    }
}

void drawRadar(Surface& r, const Space& space, const HudColours& colours, int marked) {
    if (r.width() != 17 || r.height() != 17) {
        r = Surface(17, 17);
    }
    r.fill(0);
    r.set(8, 8, colours.radarCentre);
    const Object& player = space.object(Space::kPlayer);
    for (int i = 0; i < Space::kShips; ++i) {
        const int index = Space::kFirstShip + i;
        const Object& o = space.object(index);
        if (o.kind != Kind::Ship || (o.cloaked && o.cloakLevel >= 20)) {
            continue;
        }
        const Vec v = apply(player.axes, o.pos - player.pos);
        const double across = std::hypot(static_cast<double>(v.x), static_cast<double>(v.y));
        // How far off the bow (0 ahead, 180 degrees behind) sets the
        // distance from the middle; the direction is where it lies.
        const double off = std::numbers::pi / 2 - std::atan2(static_cast<double>(v.z), across);
        const double radius = 16.0 * std::sin(off / 2);
        const double dir = std::atan2(static_cast<double>(v.y), static_cast<double>(v.x));
        const int x = static_cast<int>((radius * std::cos(dir) + 1) / 2) + 8;
        const int y = static_cast<int>((radius * -std::sin(dir) + 1) / 2) + 8;
        const std::uint8_t c = colours.radarShips[static_cast<std::size_t>(i)];
        r.set(x, y, c);
        if (index == marked) {
            r.set(x - 1, y, c);
            r.set(x + 1, y, c);
            r.set(x, y - 1, c);
            r.set(x, y + 1, c);
        }
    }
}

} // namespace helmsman::space
