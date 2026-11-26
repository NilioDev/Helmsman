// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/movie.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cstring>

namespace helmsman {

namespace {

constexpr char kMagic[] = "Interplay MVE File\x1A";
constexpr std::size_t kHeaderSize = 26;

namespace op {
constexpr int kEnd = 0;
constexpr int kEndChunk = 1;
constexpr int kTimer = 2;
constexpr int kSoundInit = 3;
constexpr int kVideoInit = 5;
constexpr int kVideo = 6;
constexpr int kShow = 7;
constexpr int kSound = 8;
constexpr int kSilence = 9;
constexpr int kPalette = 12;
constexpr int kSkipList = 14;
constexpr int kCodeList = 15;
constexpr int kVideoSkipped = 16;
} // namespace op

constexpr int kBlock = 8;
constexpr std::size_t kVideoHeader = 14;

std::vector<std::uint16_t> words(std::span<const std::uint8_t> body) {
    std::vector<std::uint16_t> out(body.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint16_t>(body[2 * i] | (body[2 * i + 1] << 8));
    }
    return out;
}

} // namespace

Movie::Movie(std::vector<std::uint8_t> file) : file_(std::move(file)) {
    if (file_.size() < kHeaderSize || std::memcmp(file_.data(), kMagic, sizeof kMagic - 1) != 0) {
        throw FormatError("not a film");
    }
    pos_ = kHeaderSize;
}

std::vector<std::uint8_t> Movie::takeSound() {
    std::vector<std::uint8_t> out;
    out.swap(sound_);
    return out;
}

bool Movie::next() {
    while (!ended_) {
        if (op_ + 4 > chunkEnd_) {
            // On to the next chunk: a length and a kind we do not need.
            if (pos_ + 4 > file_.size()) {
                ended_ = true;
                break;
            }
            Reader r(std::span<const std::uint8_t>(file_).subspan(pos_, 4));
            const std::size_t length = r.u16();
            op_ = pos_ + 4;
            chunkEnd_ = std::min(op_ + length, file_.size());
            pos_ = op_ + length;
            continue;
        }
        Reader r(std::span<const std::uint8_t>(file_).subspan(op_, 4));
        const std::size_t length = r.u16();
        const int type = r.u8();
        const int version = r.u8();
        const std::size_t start = op_ + 4;
        if (start + length > chunkEnd_) {
            throw FormatError("film operation runs past its chunk");
        }
        op_ = start + length;
        const auto body = std::span<const std::uint8_t>(file_).subspan(start, length);

        switch (type) {
        case op::kEnd:
            ended_ = true;
            break;
        case op::kEndChunk:
            op_ = chunkEnd_;
            break;
        case op::kTimer: {
            Reader t(body);
            const auto rate = t.u32();
            const auto steps = t.u16();
            frameMicros_ = static_cast<int>(std::min<std::uint64_t>(std::uint64_t{rate} * steps, 1000000));
            break;
        }
        case op::kSoundInit:
            soundInit(body, version);
            break;
        case op::kVideoInit:
            videoInit(body);
            break;
        case op::kVideo: {
            if (width_ == 0 || body.size() < kVideoHeader) {
                throw FormatError("film picture before its size");
            }
            const std::size_t blocks = static_cast<std::size_t>(width_ / kBlock) * (height_ / kBlock);
            if (body.size() < kVideoHeader + blocks * 2) {
                throw FormatError("film picture too short");
            }
            const auto map = words(body.subspan(kVideoHeader, blocks * 2));
            decode(map, std::vector<bool>(blocks, true), body.subspan(kVideoHeader + blocks * 2));
            break;
        }
        case op::kSkipList:
            skipWords_ = words(body);
            break;
        case op::kCodeList:
            codes_ = words(body);
            break;
        case op::kVideoSkipped: {
            if (width_ == 0 || body.size() < kVideoHeader) {
                throw FormatError("film picture before its size");
            }
            // Each skip word gives up to 15 blocks, highest bit first; its
            // lowest set bit only marks where the word ends.
            const std::size_t blocks = static_cast<std::size_t>(width_ / kBlock) * (height_ / kBlock);
            std::vector<bool> changed(blocks, false);
            std::vector<std::uint16_t> map(blocks, 0);
            std::size_t word = 0;
            std::size_t code = 0;
            std::uint16_t bits = 0;
            for (std::size_t i = 0; i < blocks; ++i) {
                if (bits == 0 || bits == 0x8000) {
                    if (word >= skipWords_.size()) {
                        throw FormatError("film skip list too short");
                    }
                    bits = skipWords_[word++];
                }
                if ((bits & 0x8000) != 0) {
                    if (code >= codes_.size()) {
                        throw FormatError("film code list too short");
                    }
                    changed[i] = true;
                    map[i] = codes_[code++];
                }
                bits = static_cast<std::uint16_t>(bits << 1);
            }
            decode(map, changed, body.subspan(kVideoHeader));
            break;
        }
        case op::kShow:
            return true;
        case op::kSound:
        case op::kSilence:
            sound(body, type == op::kSilence);
            break;
        case op::kPalette:
            setPalette(body);
            break;
        default:
            break; // nothing this player needs
        }
    }
    return false;
}

void Movie::videoInit(std::span<const std::uint8_t> body) {
    Reader r(body);
    const int wide = r.u16();
    const int high = r.u16();
    if (wide <= 0 || high <= 0 || wide > kMaxBlocksWide || high > kMaxBlocksHigh) {
        throw FormatError("film picture size out of range");
    }
    width_ = wide * kBlock;
    height_ = high * kBlock;
    shown_.assign(static_cast<std::size_t>(width_) * height_, 0);
    back_ = shown_;
}

void Movie::soundInit(std::span<const std::uint8_t> body, int version) {
    Reader r(body);
    r.u16();
    const auto flags = r.u16();
    const int rate = r.u16();
    (void)version; // the buffer size that follows is not needed
    soundChannels_ = (flags & 1) != 0 ? 2 : 1;
    soundWide_ = (flags & 2) != 0;
    // Packed sound (flag 4) is not used by the films we know: leave it out.
    soundRate_ = (flags & 4) != 0 || rate < 1000 ? 0 : rate;
}

void Movie::sound(std::span<const std::uint8_t> body, bool silence) {
    Reader r(body);
    r.u16(); // a running number
    const auto streams = r.u16();
    const std::size_t length = r.u16();
    if (soundRate_ == 0 || (streams & 1) == 0) {
        return;
    }
    const std::size_t frame = static_cast<std::size_t>(soundChannels_) * (soundWide_ ? 2 : 1);
    const std::size_t samples = length / frame;
    if (silence) {
        sound_.insert(sound_.end(), samples, 0x80);
        return;
    }
    const auto data = r.bytes(samples * frame);
    for (std::size_t i = 0; i < samples; ++i) {
        int sum = 0;
        for (int c = 0; c < soundChannels_; ++c) {
            const std::size_t at = (i * soundChannels_ + c) * (soundWide_ ? 2 : 1);
            // 16-bit sound is signed: keep its high byte, made unsigned.
            sum += soundWide_ ? (static_cast<std::int8_t>(data[at + 1]) + 128) : data[at];
        }
        sound_.push_back(static_cast<std::uint8_t>(sum / soundChannels_));
    }
}

void Movie::setPalette(std::span<const std::uint8_t> body) {
    Reader r(body);
    const int first = r.u16();
    const int count = r.u16();
    for (int i = 0; i < count && first + i < 256; ++i) {
        auto& c = palette_[static_cast<std::size_t>(first + i)];
        c.r = static_cast<std::uint8_t>(r.u8() & 0x3F);
        c.g = static_cast<std::uint8_t>(r.u8() & 0x3F);
        c.b = static_cast<std::uint8_t>(r.u8() & 0x3F);
    }
}

void Movie::decode(std::span<const std::uint16_t> map, const std::vector<bool>& changed,
                   std::span<const std::uint8_t> raw) {
    const int across = width_ / kBlock;
    const auto total = static_cast<std::ptrdiff_t>(shown_.size());
    // Two pictures take turns: the new one is drawn over the one shown
    // before last, and may copy from the one shown last.
    const auto& before = shown_;
    auto& now = back_;
    auto origin = [&](std::size_t block) {
        const int x = static_cast<int>(block % across) * kBlock;
        const int y = static_cast<int>(block / across) * kBlock;
        return static_cast<std::ptrdiff_t>(y) * width_ + x;
    };

    // Raw blocks first, in order, so that copies may use them.
    Reader pixels(raw);
    for (std::size_t i = 0; i < map.size(); ++i) {
        if (!changed[i] || map[i] != 0) {
            continue;
        }
        const auto at = origin(i);
        for (int row = 0; row < kBlock; ++row) {
            const auto line = pixels.bytes(kBlock);
            std::copy(line.begin(), line.end(), now.begin() + at + static_cast<std::ptrdiff_t>(row) * width_);
        }
    }

    // Copies: with the high bit set from the picture shown before, else
    // from the one being drawn, at a distance counted along the rows.
    for (std::size_t i = 0; i < map.size(); ++i) {
        const auto code = map[i];
        if (!changed[i] || code == 0) {
            continue;
        }
        const bool fromBefore = (code & 0x8000) != 0;
        const std::ptrdiff_t distance = static_cast<std::ptrdiff_t>(code) - (fromBefore ? 0xC000 : 0x4000);
        const auto& source = fromBefore ? before : now;
        const auto at = origin(i);
        for (int row = 0; row < kBlock; ++row) {
            const auto to = at + static_cast<std::ptrdiff_t>(row) * width_;
            const auto from = to + distance;
            if (from < 0 || from + kBlock > total) {
                continue;
            }
            std::memmove(now.data() + to, source.data() + from, kBlock);
        }
    }
    shown_.swap(back_);
}

} // namespace helmsman
