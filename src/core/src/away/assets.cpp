// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/away/assets.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cctype>

namespace helmsman::away {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<AnimFrame> parseAnim(std::span<const std::uint8_t> bytes) {
    std::vector<AnimFrame> frames;
    Reader r(bytes);
    while (r.remaining() >= 22) {
        AnimFrame f;
        f.picture = lower(r.fixedText(10));
        while (!f.picture.empty() && f.picture.back() == ' ') {
            f.picture.pop_back();
        }
        f.x = r.s16();
        f.y = r.s16();
        f.depth = r.s16();
        f.ticks = r.u16();
        for (auto& n : f.next) {
            n = r.u8();
        }
        frames.push_back(std::move(f));
    }
    return frames;
}

namespace {

// Which of `from` source positions each of `to` target positions shows,
// spread evenly the way a line is drawn.
std::vector<int> spread(int from, int to) {
    std::vector<int> pick;
    pick.reserve(static_cast<std::size_t>(to));
    if (from >= to) {
        // Shrinking: walk the source, keeping some.
        int err = 2 * to - from;
        for (int i = 0; i < from && static_cast<int>(pick.size()) < to; ++i) {
            if (err < 0) {
                err += 2 * to;
            } else {
                err += 2 * (to - from);
                pick.push_back(i);
            }
        }
    } else {
        // Growing: walk the target, repeating some.
        int src = 0;
        int err = 2 * (from - 1) - (to - 1);
        for (int i = 0; i < to; ++i) {
            pick.push_back(std::min(src, from - 1));
            if (err < 0) {
                err += 2 * (from - 1);
            } else {
                err += 2 * ((from - 1) - (to - 1));
                ++src;
            }
        }
    }
    while (static_cast<int>(pick.size()) < to) {
        pick.push_back(from - 1);
    }
    return pick;
}

std::vector<int> spreadRows(int from, int to) {
    // Rows: the first and last always show.
    std::vector<int> pick;
    const int a = to - 1;
    const int b = from - 1;
    if (to <= from) {
        int err = 0;
        for (int i = 0; i <= b && static_cast<int>(pick.size()) < to; ++i) {
            if (err < 0) {
                err += 2 * a;
            } else {
                err += 2 * (a - b);
                pick.push_back(i);
            }
        }
    } else {
        int src = 0;
        int err = 2 * b - a;
        for (int i = 0; i <= a; ++i) {
            pick.push_back(std::min(src, from - 1));
            if (err < 0) {
                err += 2 * b;
            } else {
                err += 2 * (b - a);
                ++src;
            }
        }
    }
    while (static_cast<int>(pick.size()) < to) {
        pick.push_back(from - 1);
    }
    return pick;
}

} // namespace

Bitmap scaleBitmap(const Bitmap& from, int scale) {
    if (scale == 256 || from.width() == 0 || from.height() == 0) {
        return from;
    }
    const int w = std::max(1, from.width() * scale >> 8);
    const int h = std::max(1, from.height() * scale >> 8);
    Bitmap out;
    out.handleX = from.handleX * scale >> 8;
    out.handleY = from.handleY * scale >> 8;
    out.pixels = Surface(w, h);
    const auto cols = spread(from.width(), w);
    const auto rows = spreadRows(from.height(), h);
    for (int y = 0; y < h; ++y) {
        const int sy = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < w; ++x) {
            out.pixels.set(x, y, from.pixels.at(cols[static_cast<std::size_t>(x)], sy));
        }
    }
    return out;
}

bool Assets::hasPicture(std::string_view name) const { return archive_.has(upper(name) + ".BMP"); }

Bitmap Assets::buildCrewPicture(const std::string& name) {
    // Only the standing and beaming pictures of Kirk, Spock and the
    // security officer are kept as differences.
    const bool shared = name.size() > 5 && (name.compare(1, 4, "stnd") == 0 || name.compare(1, 4, "tele") == 0);
    const char who = name.empty() ? 0 : name[0];
    if (shared && (who == 'k' || who == 's' || who == 'r') && archive_.has(upper(name) + ".XOR")) {
        std::string base = name;
        base[0] = 'm';
        if (archive_.has(upper(base) + ".BMP")) {
            Bitmap out = Bitmap::fromBytes(archive_.need(upper(base) + ".BMP"));
            const auto diffBytes = archive_.need(upper(name) + ".XOR");
            Reader r(diffBytes);
            const int dx = out.handleX - r.s16();
            const int dy = out.handleY - r.s16();
            const int w = r.u16();
            const int h = r.u16();
            const int shift = who == 'k' ? 8 : who == 'r' ? -8 : 0;
            // Uniform colours: Kirk's are 8 above the doctor's, the
            // security officer's 8 below.
            if (shift != 0) {
                std::uint8_t* p = out.pixels.data();
                for (std::size_t i = 0; i < out.pixels.size(); ++i) {
                    if (p[i] >= 0xA8 && p[i] <= 0xAF) {
                        p[i] = static_cast<std::uint8_t>(p[i] + shift);
                    }
                }
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const std::uint8_t v = r.remaining() > 0 ? r.u8() : 0;
                    const int tx = x + dx;
                    const int ty = y + dy;
                    if (out.pixels.contains(tx, ty)) {
                        out.pixels.set(tx, ty, static_cast<std::uint8_t>(out.pixels.at(tx, ty) ^ v));
                    }
                }
            }
            return out;
        }
    }
    return Bitmap::fromBytes(archive_.need(upper(name) + ".BMP"));
}

std::shared_ptr<const Bitmap> Assets::picture(std::string_view name) {
    const std::string key = lower(name);
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        return it->second;
    }
    auto bitmap = std::make_shared<const Bitmap>(buildCrewPicture(key));
    pictures_.emplace(key, bitmap);
    return bitmap;
}

std::shared_ptr<const Bitmap> Assets::picture(std::string_view name, int scale) {
    if (scale == 256) {
        return picture(name);
    }
    auto key = std::make_pair(lower(name), scale);
    if (const auto it = scaled_.find(key); it != scaled_.end()) {
        return it->second;
    }
    auto bitmap = std::make_shared<const Bitmap>(scaleBitmap(*picture(name), scale));
    if (scaled_.size() > 600) {
        scaled_.clear();
    }
    scaled_.emplace(std::move(key), bitmap);
    return bitmap;
}

const std::vector<AnimFrame>& Assets::anim(std::string_view name) {
    const std::string key = lower(name);
    if (const auto it = anims_.find(key); it != anims_.end()) {
        return it->second;
    }
    std::vector<AnimFrame> frames;
    if (const auto bytes = archive_.load(upper(key) + ".ANM")) {
        frames = parseAnim(*bytes);
    }
    return anims_.emplace(key, std::move(frames)).first->second;
}

} // namespace helmsman::away
