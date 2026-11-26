// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing space: the stars, then everything else from the farthest to the
// nearest. A ship is drawn from the picture of it seen from the side
// nearest to where the viewer is, stretched and turned to match how it
// lies exactly.
#include "helmsman/space/space.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace helmsman::space {

namespace {

// The picture sets of the ships seen from many sides.
constexpr const char* kPictureSets[4] = {"ENT", "KLING", "ORION", "ROM"};

struct ViewPicture {
    Mat axes;
    std::shared_ptr<const Bitmap> bitmap;
};

std::map<std::string, ViewPicture>& viewCache() {
    static std::map<std::string, ViewPicture> cache;
    return cache;
}

} // namespace

int Space::project(std::int32_t v, std::int32_t z) const {
    if (z <= 0) {
        return 0;
    }
    return static_cast<int>(static_cast<std::int64_t>(v) * focal_ / z);
}

void Space::placeCamera() {
    camera_ = player().pos;
    cameraAxes_ = player().axes;
}

void Space::lookAt(Object& o) {
    const Vec rel = o.pos - camera_;
    o.viewPos = apply(cameraAxes_, rel);
    o.viewAxes = o.axes * transpose(cameraAxes_);
    o.distance = isqrt(static_cast<std::int64_t>(rel.x) * rel.x + static_cast<std::int64_t>(rel.y) * rel.y +
                       static_cast<std::int64_t>(rel.z) * rel.z);
    o.seen = o.viewPos.z > kNear;
    if (o.seen) {
        o.screenX = centreX_ + project(o.viewPos.x, o.viewPos.z);
        o.screenY = centreY_ - project(o.viewPos.y, o.viewPos.z);
    }
}

void Space::drawStars(Surface& to) {
    const int spreadX = std::max(1, halfW_ * 3 / 2);
    const int spreadY = std::max(1, halfH_ * 3 / 2);
    for (std::size_t i = 0; i < stars_.size(); ++i) {
        Star& s = stars_[i];
        if (!s.live) {
            const int sx = static_cast<int>(rng_() & 0x7FFF) % spreadX - spreadX / 2;
            const int sy = static_cast<int>(rng_() & 0x7FFF) % spreadY - spreadY / 2;
            const std::int32_t z = focal_ << 3;
            const Vec local{static_cast<std::int32_t>(static_cast<std::int64_t>(sx) * z / focal_),
                            static_cast<std::int32_t>(static_cast<std::int64_t>(sy) * z / focal_), z};
            s.pos = camera_ + applyTransposed(cameraAxes_, local);
            s.live = true;
        }
        const Vec v = apply(cameraAxes_, s.pos - camera_);
        if (v.z <= kNear || v.z >= 0x4000 || std::abs(v.x) > v.z || std::abs(v.y) > v.z) {
            s.live = false;
            continue;
        }
        if (i >= starPictures_.size()) {
            continue;
        }
        const Bitmap& b = starPictures_[i];
        const int left = centreX_ + project(v.x, v.z);
        const int top = centreY_ - project(v.y, v.z);
        for (int y = 0; y < b.height(); ++y) {
            for (int x = 0; x < b.width(); ++x) {
                const std::uint8_t c = b.pixels.at(x, y);
                if (c != 0 && view_.contains(left + x, top + y)) {
                    to.set(left + x, top + y, c);
                }
            }
        }
    }
}

void Space::drawPicture(Surface& to, const Bitmap& b, bool mirrored, const double a[4], int x, int y) {
    // a maps the picture's own offsets (right, up) to the view's; draw by
    // mapping each pixel of the view back into the picture.
    const double det = a[0] * a[3] - a[1] * a[2];
    if (std::abs(det) < 1e-9) {
        return;
    }
    const double inv[4] = {a[3] / det, -a[1] / det, -a[2] / det, a[0] / det};
    const int w = b.width();
    const int h = b.height();
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    const double corners[4][2] = {{-b.handleX - 1.0, -b.handleY - 1.0},
                                  {w - b.handleX + 1.0, -b.handleY - 1.0},
                                  {-b.handleX - 1.0, h - b.handleY + 1.0},
                                  {w - b.handleX + 1.0, h - b.handleY + 1.0}};
    for (const auto& c : corners) {
        const double vx = a[0] * c[0] + a[1] * -c[1];
        const double vy = a[2] * c[0] + a[3] * -c[1];
        minX = std::min(minX, vx);
        maxX = std::max(maxX, vx);
        minY = std::min(minY, -vy);
        maxY = std::max(maxY, -vy);
    }
    const int left = std::max(view_.left, x + static_cast<int>(std::floor(minX)));
    const int right = std::min(view_.right, x + static_cast<int>(std::ceil(maxX)) + 1);
    const int top = std::max(view_.top, y + static_cast<int>(std::floor(minY)));
    const int bottom = std::min(view_.bottom, y + static_cast<int>(std::ceil(maxY)) + 1);
    for (int sy = top; sy < bottom; ++sy) {
        for (int sx = left; sx < right; ++sx) {
            const double vx = sx - x + 0.5;
            const double vy = -(sy - y + 0.5);
            const double pa = inv[0] * vx + inv[1] * vy;
            const double pb = -(inv[2] * vx + inv[3] * vy);
            const int px = static_cast<int>(std::floor(pa)) + b.handleX;
            const int py = static_cast<int>(std::floor(pb)) + b.handleY;
            if (px < 0 || py < 0 || px >= w || py >= h) {
                continue;
            }
            const std::uint8_t c = b.pixels.at(mirrored ? w - 1 - px : px, py);
            if (c != 0) {
                to.set(sx, sy, c);
            }
        }
    }
}

void Space::drawBeam(Surface& to, const Object& o) {
    // A phaser beam: a band from 200 behind the shot to 200 ahead of it,
    // wider where it is nearer, in three shades.
    const Vec f = o.viewAxes.row(2);
    const Vec d{static_cast<std::int32_t>(static_cast<std::int64_t>(f.x) * 200 / kOne),
                static_cast<std::int32_t>(static_cast<std::int64_t>(f.y) * 200 / kOne),
                static_cast<std::int32_t>(static_cast<std::int64_t>(f.z) * 200 / kOne)};
    const Vec a = o.viewPos - d;
    const Vec b = o.viewPos + d;
    if (a.z < kNear || b.z < kNear) {
        return;
    }
    const double ax = centreX_ + project(a.x, a.z);
    const double ay = centreY_ - project(a.y, a.z);
    const double bx = centreX_ + project(b.x, b.z);
    const double by = centreY_ - project(b.y, b.z);
    const double wa = std::clamp(project(o.size >> 3, a.z), 1, 99);
    const double wb = std::clamp(project(o.size >> 3, b.z), 1, 99);
    const double len = std::hypot(bx - ax, by - ay);
    const int steps = std::max(1, static_cast<int>(len * 2));
    const std::uint8_t shades[3] = {static_cast<std::uint8_t>(o.colour + 2), o.colour, static_cast<std::uint8_t>(o.colour + 4)};
    const double widths[3] = {1.0, 0.6, 0.25};
    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double cx = ax + (bx - ax) * t;
            const double cy = ay + (by - ay) * t;
            const double r = (wa + (wb - wa) * t) * widths[layer] / 2.0;
            const int r0 = static_cast<int>(std::floor(cx - r));
            const int r1 = static_cast<int>(std::ceil(cx + r));
            const int c0 = static_cast<int>(std::floor(cy - r));
            const int c1 = static_cast<int>(std::ceil(cy + r));
            for (int yy = c0; yy <= c1; ++yy) {
                for (int xx = r0; xx <= r1; ++xx) {
                    const double dx = xx + 0.5 - cx;
                    const double dy = yy + 0.5 - cy;
                    if (dx * dx + dy * dy <= r * r + 0.25 && view_.contains(xx, yy)) {
                        to.set(xx, yy, shades[layer]);
                    }
                }
            }
        }
    }
}

void Space::drawObject(Surface& to, Object& o) {
    if (!o.seen) {
        return;
    }
    const double z = o.viewPos.z;
    auto scaleFor = [&](int k) { return std::clamp(static_cast<double>(focal_) * k / z, 0.01, 10.0); };
    if (o.kind == Kind::Shot) {
        if (o.type == kPhaser) {
            drawBeam(to, o);
            return;
        }
        std::string name;
        int k = o.scale;
        switch (o.type) {
        case kTorpedo: name = (o.life & 1) ? "shot2" : "shot1"; break;
        case kPlasma:
            name = "plasm" + std::to_string(ticks_ & 3);
            k = o.scale + static_cast<int>((ticks_ / 4) % 8);
            break;
        case kBlast: name = "expl0" + std::to_string(o.life & 3); break;
        default: return;
        }
        if (const auto b = picture(name)) {
            const double s = scaleFor(k);
            const double a[4] = {s, 0, 0, s};
            drawPicture(to, *b, false, a, o.screenX, o.screenY);
        }
        if (o.type == kBlast && o.life == 5 && sound) {
            sound(0x26);
        }
        return;
    }
    if (o.kind == Kind::Still) {
        if (const auto b = picture(o.stillName)) {
            const double s = scaleFor(o.scale);
            const double a[4] = {s, 0, 0, s};
            drawPicture(to, *b, false, a, o.screenX, o.screenY);
        }
        return;
    }
    if (o.kind != Kind::Ship) {
        return;
    }
    if (!o.destroyed || o.blastFrame < 21) {
        if (o.cloakLevel < 28 || !o.cloaked) {
            // Where the viewer stands, as the ship sees it.
            const Vec v = apply(o.viewAxes, -o.viewPos);
            const double len = std::max(1.0, std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y +
                                                       static_cast<double>(v.z) * v.z));
            const int heading = (angleOf(v.x, v.z) + 0x2A) & (kTurn - 1);
            const int sector = heading * 12 / kTurn;
            const int fromTop = static_cast<int>(std::acos(std::clamp(v.y / len, -1.0, 1.0)) / std::numbers::pi * 512.0);
            const int level = std::min(6, (fromTop * 6 + 0x100) / 0x200);
            int around = sector;
            bool mirrored = false;
            if (sector > 6) {
                around = 12 - sector;
                mirrored = true;
            }
            if (level == 0 || level == 6) {
                around = 0;
                mirrored = false;
            }
            char name[16];
            std::snprintf(name, sizeof name, "%s%d%d", kPictureSets[std::clamp(o.type, 0, 3)], around, level);
            auto& cache = viewCache();
            auto found = cache.find(name);
            if (found == cache.end()) {
                ViewPicture vp;
                if (const auto data = archive_.load(std::string(name) + ".R3S"); data && data->size() > 36) {
                    try {
                        Reader r(*data);
                        for (auto& e : vp.axes.m) {
                            e = r.s16();
                        }
                        vp.bitmap = std::make_shared<const Bitmap>(
                            Bitmap::fromBytes(std::span<const std::uint8_t>(*data).subspan(36)));
                    } catch (const FormatError&) {
                    }
                }
                found = cache.emplace(name, vp).first;
            }
            if (found->second.bitmap) {
                Mat m = found->second.axes;
                if (mirrored) {
                    m.at(0, 1) = static_cast<std::int16_t>(-m.at(0, 1));
                    m.at(0, 2) = static_cast<std::int16_t>(-m.at(0, 2));
                    m.at(1, 0) = static_cast<std::int16_t>(-m.at(1, 0));
                    m.at(2, 0) = static_cast<std::int16_t>(-m.at(2, 0));
                }
                // The picture shows the ship's axes as `m` puts them; find the
                // stretch that puts them where `viewAxes` has them, using the
                // two axes the picture shows most clearly.
                double best = 0;
                int bi = 0;
                int bj = 1;
                for (int i = 0; i < 3; ++i) {
                    for (int j = i + 1; j < 3; ++j) {
                        const double d = std::abs(static_cast<double>(m.at(i, 0)) * m.at(j, 1) -
                                                  static_cast<double>(m.at(i, 1)) * m.at(j, 0));
                        if (d > best) {
                            best = d;
                            bi = i;
                            bj = j;
                        }
                    }
                }
                double a[4] = {1, 0, 0, 1};
                const double p00 = m.at(bi, 0), p01 = m.at(bj, 0), p10 = m.at(bi, 1), p11 = m.at(bj, 1);
                const double det = p00 * p11 - p01 * p10;
                if (std::abs(det) > 1.0) {
                    const double i00 = p11 / det, i01 = -p01 / det, i10 = -p10 / det, i11 = p00 / det;
                    const double r00 = o.viewAxes.at(bi, 0), r01 = o.viewAxes.at(bj, 0);
                    const double r10 = o.viewAxes.at(bi, 1), r11 = o.viewAxes.at(bj, 1);
                    a[0] = r00 * i00 + r01 * i10;
                    a[1] = r00 * i01 + r01 * i11;
                    a[2] = r10 * i00 + r11 * i10;
                    a[3] = r10 * i01 + r11 * i11;
                }
                const double s = scaleFor(o.scale);
                for (double& e : a) {
                    e *= s;
                }
                drawPicture(to, *found->second.bitmap, mirrored, a, o.screenX, o.screenY);
            }
        }
    }
    if (o.destroyed && o.blastFrame >= 0) {
        char name[16];
        std::snprintf(name, sizeof name, "BigExp%02d", std::min(o.blastFrame, 45));
        if (const auto b = picture(name)) {
            const double s = scaleFor(o.blastFrame >= 24 ? 12 : 6);
            const double a[4] = {s, 0, 0, s};
            drawPicture(to, *b, false, a, o.screenX, o.screenY);
        }
    }
}

void Space::drawStatic(Surface& to) {
    char name[16];
    std::snprintf(name, sizeof name, "STATIC%d.BMP", static_cast<int>(rng_() % 5));
    if (const auto data = archive_.load(name)) {
        try {
            const Bitmap b = Bitmap::fromBytes(*data);
            for (int y = view_.top; y < view_.bottom; ++y) {
                for (int x = view_.left; x < view_.right; ++x) {
                    const int bx = (x - view_.left) % std::max(1, b.width());
                    const int by = (y - view_.top) % std::max(1, b.height());
                    const std::uint8_t c = b.pixels.at(bx, by);
                    if (c != 0) {
                        to.set(x, y, c);
                    }
                }
            }
        } catch (const FormatError&) {
        }
    }
}

void Space::draw(Surface& to) {
    placeCamera();
    to.fillRect(view_.left, view_.top, view_.right - view_.left, view_.bottom - view_.top, 0);
    drawStars(to);
    std::vector<Object*> shown;
    for (int i = 0; i < kObjects; ++i) {
        Object& o = object(i);
        if (o.kind == Kind::None) {
            continue;
        }
        if (o.kind == Kind::Ship) {
            if (o.destroyed && ++o.blastFrame > 45) {
                if (i == kPlayer) {
                    playerDead_ = true;
                } else {
                    remove(i);
                    continue;
                }
            }
            o.cloakLevel = o.cloaked ? std::min(o.cloakLevel + 1, 28) : std::max(o.cloakLevel - 1, 0);
        }
        if (i == kPlayer) {
            continue;
        }
        lookAt(o);
        if (o.seen) {
            shown.push_back(&o);
        }
    }
    std::stable_sort(shown.begin(), shown.end(), [](const Object* a, const Object* b) { return a->distance > b->distance; });
    for (Object* o : shown) {
        drawObject(to, *o);
    }
    if (static_) {
        drawStatic(to);
    }
}

} // namespace helmsman::space
