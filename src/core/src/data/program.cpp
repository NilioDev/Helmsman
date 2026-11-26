// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/program.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>

namespace helmsman {

namespace {

constexpr std::uint16_t kSignature = 0x5A4D;    // "MZ"
constexpr std::uint16_t kPackedMark = 0x4252;   // "RB", ends the unpacker's header
constexpr std::size_t kMostUnpacked = 1u << 20; // a real-mode program fits in a megabyte

std::uint16_t word(std::span<const std::uint8_t> b, std::size_t at) {
    if (at + 2 > b.size()) {
        throw FormatError("program: reading past its end");
    }
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

// Unpacks the image when its start code is the unpacker; returns false
// when it is not packed.
bool unpack(std::span<const std::uint8_t> image, std::uint16_t startSegment, std::vector<std::uint8_t>& out) {
    const std::size_t at = static_cast<std::size_t>(startSegment) * 16;
    if (at + 16 > image.size()) {
        return false;
    }
    // The unpacker's header: where the real program starts, how large it
    // is when unpacked (in 16-byte paragraphs), and the mark. Some linkers
    // put one more word before the mark.
    const bool longer = at + 18 <= image.size() && word(image, at + 16) == kPackedMark;
    if (word(image, at + 14) != kPackedMark && !longer) {
        return false;
    }
    const std::size_t unpacked = static_cast<std::size_t>(word(image, at + 12)) * 16;
    if (unpacked < at || unpacked > kMostUnpacked) {
        throw FormatError("program: the unpacked size does not fit");
    }
    const std::span<const std::uint8_t> packed = image.first(at);
    out.assign(unpacked, 0);
    std::copy(packed.begin(), packed.end(), out.begin());
    // The packed data is read backwards from its end (after any 0xFF
    // padding) and the program is written backwards from the end of the
    // unpacked size: a command byte, a 16-bit count, then either one byte
    // to repeat (command B0/B1) or that many bytes to copy (B2/B3). An odd
    // command is the last one.
    std::size_t src = packed.size();
    while (src > 0 && packed[src - 1] == 0xFF) {
        --src;
    }
    std::size_t dst = out.size();
    for (;;) {
        if (src < 3) {
            throw FormatError("program: packed data ends early");
        }
        const std::uint8_t command = packed[src - 1];
        const std::size_t count = static_cast<std::size_t>(packed[src - 2]) << 8 | packed[src - 3];
        src -= 3;
        if (count > dst) {
            throw FormatError("program: packed data runs before the start");
        }
        if ((command & 0xFE) == 0xB0) {
            if (src < 1) {
                throw FormatError("program: packed data ends early");
            }
            const std::uint8_t fill = packed[--src];
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(dst - count), out.begin() + static_cast<std::ptrdiff_t>(dst), fill);
            dst -= count;
        } else if ((command & 0xFE) == 0xB2) {
            if (count > src) {
                throw FormatError("program: packed data ends early");
            }
            // Copied backwards byte by byte, so it may overlap where it is
            // written.
            for (std::size_t i = 0; i < count; ++i) {
                out[--dst] = packed[--src];
            }
        } else {
            throw FormatError("program: unknown command in packed data");
        }
        if (command & 1) {
            break;
        }
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> programImage(std::span<const std::uint8_t> file) {
    if (word(file, 0) != kSignature) {
        throw FormatError("program: not a DOS program");
    }
    const std::size_t lastPage = word(file, 2);
    const std::size_t pages = word(file, 4);
    const std::size_t headerSize = static_cast<std::size_t>(word(file, 8)) * 16;
    const std::uint16_t startSegment = word(file, 0x16);
    std::size_t end = pages * 512;
    if (lastPage != 0) {
        end = end - 512 + lastPage;
    }
    end = std::min(end, file.size());
    if (headerSize > end) {
        throw FormatError("program: header larger than the file");
    }
    const std::span<const std::uint8_t> image = file.subspan(headerSize, end - headerSize);
    std::vector<std::uint8_t> out;
    if (unpack(image, startSegment, out)) {
        return out;
    }
    return {image.begin(), image.end()};
}

} // namespace helmsman
