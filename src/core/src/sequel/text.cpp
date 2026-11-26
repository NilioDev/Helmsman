// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/sequel/text.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <limits>

namespace helmsman::sequel {

namespace {

constexpr std::size_t kGlyphBytes = 33; // three planes of 11 rows
constexpr std::size_t kGlyphs = 256;
// Characters 0x10 to 0x1A are little pictures with colours of their own.
constexpr std::uint8_t kFirstPicture = 0x10;
constexpr std::uint8_t kLastPicture = 0x1A;
constexpr int kPictureSet = 5;

} // namespace

SequelFont SequelFont::fromBytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kGlyphs * kGlyphBytes) {
        throw FormatError("font too short");
    }
    SequelFont font;
    font.glyphs_.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(kGlyphs * kGlyphBytes));
    // A plain set: greys from the standard palette.
    for (int set = 0; set < kColours; ++set) {
        for (int v = 1; v < 8; ++v) {
            font.colours_[static_cast<std::size_t>(set * 8 + v)] = static_cast<std::uint8_t>(15);
        }
    }
    return font;
}

void SequelFont::setColours(std::span<const std::uint8_t> table) {
    const std::size_t n = std::min(table.size(), colours_.size());
    std::copy_n(table.begin(), n, colours_.begin());
}

int SequelFont::pixel(std::uint8_t c, int x, int y) const {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
        return 0;
    }
    const std::size_t at = static_cast<std::size_t>(c) * kGlyphBytes + static_cast<std::size_t>(y);
    const int bit = 7 - x;
    const int p0 = (glyphs_[at] >> bit) & 1;
    const int p1 = (glyphs_[at + kHeight] >> bit) & 1;
    const int p2 = (glyphs_[at + 2 * kHeight] >> bit) & 1;
    return (p2 << 2) | (p1 << 1) | p0;
}

void SequelFont::drawChar(Surface& to, int x, int y, std::uint8_t c, int set, const std::uint8_t* shade) const {
    if (c >= kFirstPicture && c <= kLastPicture) {
        set = kPictureSet;
    } else if (set < 0 || set >= kColours) {
        set = 0;
    }
    for (int row = 0; row < kHeight; ++row) {
        for (int col = 0; col < kWidth; ++col) {
            const int px = x + col;
            const int py = y + row;
            if (!to.contains(px, py)) {
                continue;
            }
            const int v = pixel(c, col, row);
            if (v != 0) {
                to.set(px, py, colours_[static_cast<std::size_t>(set * 8 + v)]);
            } else if (shade != nullptr) {
                to.set(px, py, shade[to.at(px, py)]);
            }
        }
    }
}

void SequelFont::drawText(Surface& to, int x, int y, std::string_view text, int set, const std::uint8_t* shade) const {
    for (const char ch : text) {
        drawChar(to, x, y, static_cast<std::uint8_t>(ch), set, shade);
        x += kWidth;
    }
}

std::array<std::uint8_t, 256> shadeTable(const Palette& palette, int percent) {
    std::array<std::uint8_t, 256> out{};
    for (std::size_t i = 0; i < 256; ++i) {
        const auto& c = palette[i];
        const int r = c.r * percent / 100;
        const int g = c.g * percent / 100;
        const int b = c.b * percent / 100;
        int best = std::numeric_limits<int>::max();
        for (std::size_t j = 0; j < 256; ++j) {
            const auto& d = palette[j];
            const int dist = (d.r - r) * (d.r - r) + (d.g - g) * (d.g - g) + (d.b - b) * (d.b - b);
            if (dist < best) {
                best = dist;
                out[i] = static_cast<std::uint8_t>(j);
            }
        }
    }
    return out;
}

std::vector<std::string> wrapText(std::string_view text, std::size_t columns) {
    std::vector<std::string> rows;
    std::string row;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t end = text.find(' ', i);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string word(text.substr(i, end - i));
        i = end;
        while (word.size() > columns) {
            if (!row.empty()) {
                rows.push_back(row);
                row.clear();
            }
            rows.push_back(word.substr(0, columns));
            word.erase(0, columns);
        }
        if (row.empty()) {
            row = word;
        } else if (row.size() + 1 + word.size() <= columns) {
            row += ' ';
            row += word;
        } else {
            rows.push_back(row);
            row = word;
        }
    }
    if (!row.empty()) {
        rows.push_back(row);
    }
    return rows;
}

std::string takeSoundTag(std::string& text) {
    if (text.size() < 2 || text[0] != '#') {
        return {};
    }
    const auto end = text.find('#', 1);
    if (end == std::string::npos) {
        return {};
    }
    std::string tag = text.substr(1, end - 1);
    text.erase(0, end + 1);
    return tag;
}

} // namespace helmsman::sequel
