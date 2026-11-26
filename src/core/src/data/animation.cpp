// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/animation.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cstring>

namespace helmsman {

namespace {
constexpr std::size_t kPaletteAt = 0x100;
constexpr std::size_t kPagesAt = 0x500;
constexpr std::size_t kFirstPageAt = 0xB00;
constexpr std::size_t kPageSize = 0x10000;
constexpr int kMaxPages = 256;
constexpr int kMaxSide = 640;
} // namespace

bool Animation::isAnimation(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= kFirstPageAt && std::memcmp(bytes.data(), "LPF ", 4) == 0 &&
           std::memcmp(bytes.data() + 16, "ANIM", 4) == 0;
}

Animation::Animation(std::vector<std::uint8_t> file) : file_(std::move(file)) {
    if (!isAnimation(file_)) {
        throw FormatError("not an animation");
    }
    Reader r(file_);
    r.seek(6);
    const int pageCount = r.u16();
    records_ = static_cast<int>(std::min<std::uint32_t>(r.u32(), 0xFFFF));
    r.seek(20);
    width_ = r.u16();
    height_ = r.u16();
    r.seek(64);
    frames_ = static_cast<int>(std::min<std::uint32_t>(r.u32(), 0xFFFF));
    fps_ = r.u16();
    if (width_ <= 0 || height_ <= 0 || width_ > kMaxSide || height_ > kMaxSide || pageCount > kMaxPages ||
        frames_ <= 0 || frames_ > records_) {
        throw FormatError("animation header out of range");
    }
    // Colours are blue, green, red and a spare byte, 8 bits each.
    r.seek(kPaletteAt);
    for (std::size_t i = 0; i < 256; ++i) {
        auto& c = palette_[i];
        c.b = static_cast<std::uint8_t>(r.u8() >> 2);
        c.g = static_cast<std::uint8_t>(r.u8() >> 2);
        c.r = static_cast<std::uint8_t>(r.u8() >> 2);
        r.u8();
    }
    r.seek(kPagesAt);
    for (int i = 0; i < pageCount; ++i) {
        Page page;
        page.first = r.u16();
        page.count = r.u16();
        r.u16(); // bytes used
        pages_.push_back(page);
    }
    picture_.assign(static_cast<std::size_t>(width_) * height_, 0);
}

std::span<const std::uint8_t> Animation::record(int index) const {
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        const auto& page = pages_[i];
        if (index < page.first || index >= page.first + page.count) {
            continue;
        }
        const std::size_t at = kFirstPageAt + i * kPageSize;
        if (at >= file_.size()) {
            throw FormatError("animation page past the end");
        }
        const auto bytes = std::span<const std::uint8_t>(file_).subspan(at, std::min(kPageSize, file_.size() - at));
        Reader r(bytes);
        r.u16();
        const int count = r.u16();
        r.u16();
        r.u16();
        std::size_t offset = 8 + static_cast<std::size_t>(count) * 2;
        std::size_t size = 0;
        for (int k = 0; k < count; ++k) {
            const std::size_t length = r.u16();
            if (k < index - page.first) {
                offset += length;
            } else if (k == index - page.first) {
                size = length;
            }
        }
        r.seek(std::min(offset, bytes.size()));
        return r.bytes(size);
    }
    throw FormatError("animation frame in no page");
}

void Animation::next() {
    frame_ = frame_ + 1 >= frames_ ? 0 : frame_ + 1;
    if (frame_ == 0) {
        std::fill(picture_.begin(), picture_.end(), 0);
    }
    draw(record(frame_));
}

void Animation::draw(std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return; // the same picture again
    }
    Reader r(data);
    r.u8();             // 'B'
    const int extra = r.u8(); // words of extra data, unused here
    r.u16();
    r.skip(static_cast<std::size_t>(extra) * 2);

    std::size_t at = 0;
    const std::size_t end = picture_.size();
    auto fill = [&](std::size_t count, std::uint8_t colour) {
        if (count > end - std::min(at, end)) {
            throw FormatError("animation draws past the picture");
        }
        std::fill_n(picture_.begin() + static_cast<std::ptrdiff_t>(at), count, colour);
        at += count;
    };
    auto copy = [&](std::size_t count) {
        if (count > end - std::min(at, end)) {
            throw FormatError("animation draws past the picture");
        }
        const auto bytes = r.bytes(count);
        std::copy(bytes.begin(), bytes.end(), picture_.begin() + static_cast<std::ptrdiff_t>(at));
        at += count;
    };
    for (;;) {
        const int code = r.u8();
        if (code > 0 && code < 0x80) {
            copy(static_cast<std::size_t>(code));
        } else if (code == 0) {
            const std::size_t count = r.u8();
            fill(count, r.u8());
        } else if (code > 0x80) {
            at += static_cast<std::size_t>(code - 0x80);
        } else {
            const int word = r.u16();
            if (word == 0) {
                return;
            }
            if ((word & 0x8000) == 0) {
                at += static_cast<std::size_t>(word);
            } else if ((word & 0x4000) != 0) {
                const auto count = static_cast<std::size_t>(word & 0x3FFF);
                fill(count, r.u8());
            } else {
                copy(static_cast<std::size_t>(word & 0x3FFF));
            }
        }
    }
}

} // namespace helmsman
