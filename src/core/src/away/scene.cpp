// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/away/scene.hpp"

#include <algorithm>

namespace helmsman::away {

bool Sprite::bounds(int& left, int& top, int& w, int& h) const {
    if (mode == SpriteMode::Hidden) {
        return false;
    }
    if (mode == SpriteMode::Text) {
        if (columns <= 0) {
            return false;
        }
        left = x;
        top = y;
        w = columns * Font::kSize;
        h = static_cast<int>(text.size() / static_cast<std::size_t>(columns)) * Font::kSize;
        return w > 0 && h > 0;
    }
    if (!bitmap) {
        return false;
    }
    left = x - bitmap->handleX;
    top = y - bitmap->handleY;
    w = bitmap->width();
    h = bitmap->height();
    return w > 0 && h > 0;
}

bool Sprite::hit(int sx, int sy) const {
    int left = 0;
    int top = 0;
    int w = 0;
    int h = 0;
    if (!bounds(left, top, w, h)) {
        return false;
    }
    if (sx < left || sy < top || sx >= left + w || sy >= top + h) {
        return false;
    }
    if (mode == SpriteMode::Text || mode == SpriteMode::Overlay) {
        return true;
    }
    return bitmap->pixels.at(sx - left, sy - top) != 0;
}

void Scene::setDepths(std::span<const std::uint8_t> bytes) {
    std::fill(depths.begin(), depths.end(), 0);
    const std::size_t n = std::min(bytes.size(), depths.size() / 2);
    for (std::size_t i = 0; i < n; ++i) {
        depths[2 * i] = static_cast<std::uint8_t>(bytes[i] >> 4);
        depths[2 * i + 1] = static_cast<std::uint8_t>(bytes[i] & 0x0F);
    }
}

void sortForDrawing(std::vector<const Sprite*>& sprites) {
    std::stable_sort(sprites.begin(), sprites.end(), [](const Sprite* a, const Sprite* b) {
        if (a->depth != b->depth) {
            return a->depth < b->depth;
        }
        if (a->order != b->order) {
            return a->order < b->order;
        }
        if (a->y != b->y) {
            return a->y < b->y;
        }
        return a->x < b->x;
    });
}

void Scene::drawSprite(Surface& to, const Sprite& s) const {
    int left = 0;
    int top = 0;
    int w = 0;
    int h = 0;
    if (!s.bounds(left, top, w, h)) {
        return;
    }
    const int x0 = std::max(0, left);
    const int y0 = std::max(0, top);
    const int x1 = std::min(to.width(), left + w);
    const int y1 = std::min(to.height(), top + h);
    if (s.mode == SpriteMode::Text) {
        for (int y = y0; y < y1; ++y) {
            const int row = (y - top) / Font::kSize;
            for (int x = x0; x < x1; ++x) {
                const int col = (x - left) / Font::kSize;
                const std::uint8_t ch = s.text[static_cast<std::size_t>(row * s.columns + col)];
                std::uint8_t c = font != nullptr ? font->pixel(ch, (x - left) % Font::kSize, (y - top) % Font::kSize) : 0;
                if (c == 0) {
                    to.set(x, y, darken[to.at(x, y)]);
                    continue;
                }
                if (c == 0x78) {
                    // The border characters keep their own gold.
                    c = (ch >= 0x10 && ch <= 0x1A) ? 0xB3 : s.textColour;
                }
                to.set(x, y, c);
            }
        }
        return;
    }
    const Surface& px = s.bitmap->pixels;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::uint8_t c = px.at(x - left, y - top);
            if (s.mode == SpriteMode::Overlay) {
                to.set(x, y, c != 0 ? c : darken[to.at(x, y)]);
                continue;
            }
            if (c == 0 || depthAt(x, y) > s.depth) {
                continue;
            }
            to.set(x, y, c);
        }
    }
}

void Scene::draw(Surface& to, const std::vector<const Sprite*>& sprites) const {
    to.blit(backdrop, 0, 0, backdrop.width(), backdrop.height(), 0, 0);
    auto sorted = sprites;
    sortForDrawing(sorted);
    for (const Sprite* s : sorted) {
        drawSprite(to, *s);
    }
}

} // namespace helmsman::away
