// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/archive.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cctype>

namespace helmsman {

namespace {

constexpr std::size_t kEntrySize = 14;
constexpr std::uint32_t kRunBit = 0x800000;

std::string upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

} // namespace

std::vector<std::uint8_t> unpackLzss(std::span<const std::uint8_t> packed, std::size_t size) {
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::size_t at = 0;
    while (out.size() < size) {
        if (at >= packed.size()) {
            // A few files in the games end early; the rest is left 0.
            out.resize(size, 0);
            break;
        }
        const std::uint8_t flags = packed[at++];
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if ((flags >> bit) & 1) {
                if (at >= packed.size()) {
                    break;
                }
                out.push_back(packed[at++]);
                continue;
            }
            if (at + 2 > packed.size()) {
                at = packed.size();
                break;
            }
            const unsigned word = packed[at] | (packed[at + 1] << 8);
            at += 2;
            const std::size_t length = (word & 0x0F) + 3;
            const std::size_t back = word >> 4;
            // The unpacker works in a window that starts out all 0, so a
            // copy from before the start gives 0s.
            for (std::size_t i = 0; i < length && out.size() < size; ++i) {
                out.push_back(back != 0 && back <= out.size() ? out[out.size() - back] : std::uint8_t{0});
            }
        }
    }
    return out;
}

std::string nextInRun(std::string_view name) {
    std::string base(name);
    std::string ext;
    if (const auto dot = base.find('.'); dot != std::string::npos) {
        ext = base.substr(dot);
        base.resize(dot);
    }
    if (base.empty()) {
        return std::string(name);
    }
    std::size_t digits = 0;
    while (digits < base.size() && std::isdigit(static_cast<unsigned char>(base[base.size() - 1 - digits]))) {
        ++digits;
    }
    if (digits == 0) {
        base.back() = static_cast<char>(base.back() + 1);
        return base + ext;
    }
    // Counts up with carry, keeping the width.
    for (std::size_t i = base.size(); i-- > base.size() - digits;) {
        if (base[i] != '9') {
            ++base[i];
            return base + ext;
        }
        base[i] = '0';
    }
    return base + ext;
}

Archive Archive::open(GameSource& source, std::string_view base) {
    const std::string b(base);
    auto dir = source.read(b + ".DIR");
    auto packed = source.read(b + ".001");
    if (!dir || !packed) {
        throw FormatError("the game's " + b + " archive is missing");
    }
    const auto run = source.read(b + ".RUN").value_or(std::vector<std::uint8_t>{});
    return fromBytes(std::move(*dir), run, std::move(*packed));
}

Archive Archive::fromBytes(std::vector<std::uint8_t> dir, std::span<const std::uint8_t> run,
                           std::vector<std::uint8_t> packed) {
    Archive archive;
    archive.packed_ = std::move(packed);
    Reader r(dir);
    while (r.remaining() >= kEntrySize) {
        const std::string name = r.fixedText(8);
        const std::string ext = r.fixedText(3);
        const std::uint32_t value = r.u24();
        if (name.empty()) {
            continue;
        }
        std::string full = upper(name) + "." + upper(ext);
        if ((value & kRunBit) == 0) {
            archive.offsets_[full] = value;
            continue;
        }
        // A run: the first file's offset, then how far each next one lies
        // after the one before.
        const std::uint32_t count = (value >> 16) & 0x7F;
        Reader rr(run);
        rr.seek(value & 0xFFFF);
        std::uint32_t offset = rr.u24();
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i > 0) {
                offset += rr.u16();
                full = nextInRun(full);
            }
            archive.offsets_[full] = offset;
        }
    }
    if (archive.offsets_.empty()) {
        throw FormatError("the archive's directory is empty");
    }
    return archive;
}

bool Archive::has(std::string_view name) const { return offsets_.count(upper(name)) != 0; }

std::optional<std::vector<std::uint8_t>> Archive::load(std::string_view name) const {
    const auto it = offsets_.find(upper(name));
    if (it == offsets_.end()) {
        return std::nullopt;
    }
    Reader r(packed_);
    r.seek(it->second);
    const std::uint16_t size = r.u16();
    const std::uint16_t packedSize = r.u16();
    return unpackLzss(r.bytes(std::min<std::size_t>(packedSize, r.remaining())), size);
}

std::vector<std::uint8_t> Archive::need(std::string_view name) const {
    auto data = load(name);
    if (!data) {
        throw FormatError("the game's file " + std::string(name) + " is missing");
    }
    return std::move(*data);
}

std::vector<std::string> Archive::names() const {
    std::vector<std::pair<std::uint32_t, std::string>> sorted;
    for (const auto& [name, offset] : offsets_) {
        sorted.emplace_back(offset, name);
    }
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::string> out;
    for (auto& [offset, name] : sorted) {
        out.push_back(std::move(name));
    }
    return out;
}

} // namespace helmsman
