// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/sound_bank.hpp"

#include "helmsman/data/game_source.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/data/voc.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace helmsman {

namespace {

constexpr char kBankMagic[] = "VOCFILES";
constexpr char kPackedMagic[] = "Interplay ACMP Data";
constexpr std::size_t kPackedHeader = 26;
constexpr std::uint32_t kMaxEntries = 20000;
constexpr std::uint32_t kMaxSamples = 1u << 24;
constexpr int kBlock = 256;
// Real runs are short; a damaged sound could otherwise run on for ever.
constexpr int kMaxRun = 64;

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// The packed sound's bits. Two readers share one byte stream: one gives
// fields of bits, lowest bit first; the other gives the lengths of runs of
// equal bits, the runs taking turns between ones and zeros. Bytes past the
// end read as zero.
class Bits {
public:
    explicit Bits(std::span<const std::uint8_t> data) : data_(data) {}

    void startBlock() {
        field_ = 0;
        fieldBits_ = 0;
        runByte_ = 0;
        runBits_ = 0;
        ones_ = false;
    }

    std::uint8_t byte() { return pos_ < data_.size() ? data_[pos_++] : 0; }

    int field(int count) {
        if (count <= 0) {
            return 0;
        }
        count = std::min(count, 16);
        while (fieldBits_ < count) {
            field_ |= static_cast<std::uint32_t>(byte()) << fieldBits_;
            fieldBits_ += 8;
        }
        const auto v = static_cast<int>(field_ & ((1u << count) - 1));
        field_ >>= count;
        fieldBits_ -= count;
        return v;
    }

    // The length of the next run. A length of 10 is followed by the real
    // length in six bits.
    int run() {
        ones_ = !ones_;
        int length = 1;
        for (;;) {
            if (runBits_ == 0) {
                runByte_ = byte();
                runBits_ = 8;
            }
            --runBits_;
            const bool bit = (runByte_ & 1) != 0;
            runByte_ >>= 1;
            if (bit != ones_) {
                break;
            }
            if (++length > kMaxRun) {
                throw FormatError("packed sound run too long");
            }
        }
        return length == 10 ? field(6) : length;
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
    std::uint32_t field_ = 0;
    int fieldBits_ = 0;
    std::uint8_t runByte_ = 0;
    int runBits_ = 0;
    bool ones_ = false;
};

// A number where the lowest bit gives the sign.
int signedFromLowBit(int v) { return (v & 1) != 0 ? -(v >> 1) : v >> 1; }

} // namespace

PlainSound unpackAcmp(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kPackedHeader + 4 || std::memcmp(bytes.data(), kPackedMagic, sizeof kPackedMagic - 1) != 0) {
        throw FormatError("not a packed sound");
    }
    Reader header(bytes);
    header.seek(20);
    PlainSound out;
    out.rate = header.u16();
    if (out.rate < 1000) {
        throw FormatError("packed sound rate out of range");
    }
    Bits bits(bytes.subspan(kPackedHeader));
    std::uint32_t left = bits.byte();
    left |= static_cast<std::uint32_t>(bits.byte()) << 8;
    left |= static_cast<std::uint32_t>(bits.byte()) << 16;
    left |= static_cast<std::uint32_t>(bits.byte()) << 24;
    if (left > kMaxSamples) {
        throw FormatError("packed sound too long");
    }
    out.samples.reserve(left);

    std::uint8_t last = 0x80; // the sample before the block
    std::vector<int> half;
    while (left > 0) {
        const int count = static_cast<int>(std::min<std::uint32_t>(left, kBlock));
        bits.startBlock();
        const int head = bits.field(8);
        const int shift = (head >> 5) & 3;
        const bool halved = (head & 0x80) != 0;
        const int method = head & 0x1F;
        const int stored = halved ? (count + 1) / 2 : count;

        std::vector<int> block(static_cast<std::size_t>(stored), 0);
        int level = 0x80;
        auto step = [&](int delta) {
            level = (level + delta * (1 << shift)) & 0xFFFF;
            return level & 0xFF;
        };
        for (int i = 0; i < stored; ++i) {
            int v = 0;
            switch (method) {
            case 0: // one value for the whole block
                v = i == 0 ? bits.field(8) : block[0];
                break;
            case 1: {
                const int width = bits.run() - 1;
                v = step(width > 0 ? signedFromLowBit(bits.field(width) | (1 << std::min(width, 16))) : 0);
                break;
            }
            case 2: {
                int d = bits.field(4);
                d = d != 0 ? d - 8 : bits.field(8 - shift);
                v = step(d);
                break;
            }
            case 3: {
                const int high = bits.run() - 1;
                v = step(signedFromLowBit(bits.field(3) + high * 8));
                break;
            }
            case 4: {
                const int high = bits.run() - 1;
                v = step(signedFromLowBit(bits.field(4) + high * 16));
                break;
            }
            case 5: {
                int d = bits.field(2);
                if (d == 3) {
                    d = -1;
                } else if (d == 2) {
                    const int width = std::min(bits.run(), 15);
                    d = signedFromLowBit(bits.field(width + 1) | (2 << width));
                }
                v = step(d);
                break;
            }
            case 6: // plain samples, the low bits left out
                v = (bits.field(8 - shift) << shift) & 0xFF;
                break;
            default:
                throw FormatError("unknown packed sound block");
            }
            block[static_cast<std::size_t>(i)] = v;
        }

        if (halved) {
            // Every other sample was left out: put them back between their
            // neighbours, rounding with a gentle curve where there are two on
            // each side.
            half = block;
            block.assign(static_cast<std::size_t>(count), 0);
            for (int c = count - 1; c >= 0; --c) {
                const int s = c >> 1;
                const auto at = [&](int k) { return half[static_cast<std::size_t>(k)]; };
                if ((c & 1) != 0) {
                    block[static_cast<std::size_t>(c)] = at(s);
                    continue;
                }
                const int before = s > 0 ? at(s - 1) : last;
                if (s > 1 && s < stored - 1) {
                    const int sum = at(s) + before;
                    const int bend = sum - at(s + 1) - at(s - 2);
                    block[static_cast<std::size_t>(c)] = std::clamp((3 * bend + ((sum + 1) << 5)) >> 6, 0, 255);
                } else {
                    block[static_cast<std::size_t>(c)] = (((before + 1) & ~1) + at(s)) >> 1;
                }
            }
        }
        for (const int v : block) {
            out.samples.push_back(static_cast<std::uint8_t>(v));
        }
        last = out.samples.back();
        left -= static_cast<std::uint32_t>(count);
    }
    return out;
}

PlainSound decodeBankSound(std::span<const std::uint8_t> bytes) {
    if (bytes.size() >= sizeof kPackedMagic - 1 && std::memcmp(bytes.data(), kPackedMagic, sizeof kPackedMagic - 1) == 0) {
        return unpackAcmp(bytes);
    }
    const auto voc = Voc::parse(bytes);
    PlainSound out;
    for (const auto& part : voc.parts) {
        if (out.rate == 0) {
            out.rate = part.rate;
        }
        out.samples.insert(out.samples.end(), part.samples.begin(), part.samples.end());
    }
    return out;
}

SoundBank::SoundBank(GameSource& source, std::string path) : source_(&source), path_(std::move(path)) {
    const auto head = source.readPart(path_, 0, 16);
    if (head.size() < 16 || std::memcmp(head.data(), kBankMagic, 8) != 0) {
        throw FormatError("not a sound bank");
    }
    Reader r(head);
    r.seek(8);
    const auto listAt = r.u32();
    const auto count = r.u32();
    const auto fileSize = source.sizeOf(path_);
    if (count > kMaxEntries || listAt > fileSize || std::uint64_t{count} * 16 > fileSize - listAt) {
        throw FormatError("sound bank list out of range");
    }
    const auto list = source.readPart(path_, listAt, static_cast<std::size_t>(count) * 16);
    Reader l(list);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto name = upper(l.fixedText(8));
        Entry e;
        e.offset = l.u32();
        e.size = l.u32();
        if (e.offset > fileSize || e.size > fileSize - e.offset) {
            throw FormatError("sound bank entry out of range");
        }
        entries_.emplace(name, e);
    }
}

bool SoundBank::has(std::string_view name) const { return entries_.find(upper(name)) != entries_.end(); }

std::vector<std::string> SoundBank::names() const {
    std::vector<std::string> out;
    for (const auto& [name, e] : entries_) {
        out.push_back(name);
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> SoundBank::raw(std::string_view name) const {
    const auto it = entries_.find(upper(name));
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return source_->readPart(path_, it->second.offset, it->second.size);
}

std::optional<PlainSound> SoundBank::sound(std::string_view name) const {
    const auto bytes = raw(name);
    if (!bytes) {
        return std::nullopt;
    }
    return decodeBankSound(*bytes);
}

} // namespace helmsman
