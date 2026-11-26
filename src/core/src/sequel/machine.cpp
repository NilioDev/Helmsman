// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/sequel/machine.hpp"

#include "helmsman/data/program.hpp"
#include "helmsman/data/reader.hpp"

#include <algorithm>

namespace helmsman::sequel {

namespace {

// Where the data segment starts in the loaded program, in paragraphs.
constexpr std::size_t kDataParagraph = 0x2B87;
constexpr std::uint16_t kSaveVersion = 0x24;
constexpr std::uint16_t kSpaceVersion = 0x0311;
constexpr std::uint16_t kSpaceStart = 0x000C;
constexpr std::uint16_t kShipSize = 0x1CE;

struct Block {
    std::uint16_t at;
    std::uint16_t size;
};

} // namespace

Machine::Machine(std::span<const std::uint8_t> programFile) {
    const auto image = programImage(programFile);
    const std::size_t start = kDataParagraph * 16;
    if (image.size() <= start) {
        throw FormatError("not the sequel's program");
    }
    const std::size_t size = std::min<std::size_t>(image.size() - start, 0x10000);
    std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(start), size, cpu.memory() + X86::linear(kData, 0));
    // The check that this is the data segment: the run-time library's
    // copyright notice sits near its start in every Microsoft C program.
    const std::string probe(reinterpret_cast<const char*>(cpu.memory() + X86::linear(kData, 0)), 0x40);
    if (probe.find("MS Run-Time Library") == std::string::npos) {
        throw FormatError("not the sequel's program");
    }
}

std::string Machine::text(std::uint16_t off, std::size_t max) const {
    std::string out;
    for (std::size_t i = 0; i < max; ++i) {
        const char c = static_cast<char>(u8(static_cast<std::uint16_t>(off + i)));
        if (c == 0) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

void Machine::setText(std::uint16_t off, const std::string& s, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        set8(static_cast<std::uint16_t>(off + i), i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
    }
}

std::string Machine::scriptText(std::uint16_t seg, std::uint16_t off, std::size_t max) const {
    std::string out;
    for (std::size_t i = 0; i < max && off + i < 0x10000; ++i) {
        const char c = static_cast<char>(cpu.read8(seg, static_cast<std::uint16_t>(off + i)));
        if (c == 0) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

void Machine::loadSaved(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);
    auto block = [&](std::uint16_t at, std::uint16_t size) {
        const auto data = r.bytes(size);
        for (std::size_t i = 0; i < data.size(); ++i) {
            set8(static_cast<std::uint16_t>(at + i), data[i]);
        }
    };
    r.bytes(0x19); // the description
    if (r.u16() != kSaveVersion) {
        throw FormatError("saved game of another version");
    }
    block(ds::kState, 2);
    // Ships.
    block(0xC33A, kShipSize);
    block(0xBBBA, 0xA8);
    for (const Block b : {Block{0xC032, 0x0C}, Block{0x71A4, 0x12}, Block{0xC84A, 0x0A}, Block{0xBF70, 2}}) {
        block(b.at, b.size);
    }
    for (int i = 0; i < 4; ++i) {
        block(static_cast<std::uint16_t>(0xB446 + i * kShipSize), kShipSize);
    }
    for (const Block b : {Block{0x851A, 4}, Block{0x00C2, 2}, Block{0x00C4, 2}, Block{0xC0A6, 2}, Block{0xBC62, 4},
                          Block{0x00C6, 2}, Block{0x00CA, 2}, Block{ds::kEpisodeName, 0x10}, Block{0x6E96, 0x0C}}) {
        block(b.at, b.size);
    }
    // The space script.
    for (const Block b : {Block{ds::kResume, 2}, Block{0x5EBE, 2}, Block{0xC00A, 0x0A}, Block{0x2634, 2},
                          Block{ds::kEpisode, 2}, Block{0x6032, 0x10}, Block{0x2658, 2}, Block{0x2630, 2}}) {
        block(b.at, b.size);
    }
    if (u16(0x2630) != 0) {
        block(0x5EC0, 0x12A);
        block(0x5FEA, 2);
    }
    block(ds::kCombatLevel, 2);
    if (s16(ds::kState) == 2) {
        throw FormatError("saved games on a mission are not read yet");
    }
    block(0x851A, 4);
    if (s16(ds::kState) != -1) {
        block(ds::kActors, 32 * ds::kActorSize);
    }
    for (const Block b : {Block{ds::kScriptBlock, 0x12A}, Block{0xC7BA, 0x40}, Block{0x9AF2, 2}, Block{0xB418, 2},
                          Block{0x11DA, 0x280}}) {
        block(b.at, b.size);
    }
    // Music and sound follow; Helmsman starts those afresh.
}

void Machine::loadSpaceScript(std::span<const std::uint8_t> file) {
    if (file.size() < 0x10 || file.size() > 0xFFF0) {
        throw FormatError("space script of a wrong size");
    }
    Reader r(file);
    r.seek(6);
    if (r.u16() != kSpaceVersion) {
        throw FormatError("not a space script");
    }
    std::uint8_t* base = cpu.memory() + X86::linear(kSpace, 0);
    std::fill_n(base, 0x10000, std::uint8_t{0});
    std::copy(file.begin(), file.end(), base);
    // The far pointer the code calls the game through, and the scripts'
    // block.
    cpu.write16(kSpace, 0, kSpaceEntry);
    cpu.write16(kSpace, 2, X86::kHostSegment);
    cpu.write16(kSpace, 4, ds::kScriptBlock);
    spaceSize_ = file.size();
}

void Machine::runSpace(std::uint16_t offset, const Host& host) {
    if (spaceSize_ == 0) {
        throw FormatError("no space script");
    }
    if (offset == 0) {
        offset = kSpaceStart;
    }
    X86& c = cpu;
    c.s[X86::DS] = kData;
    c.s[X86::ES] = kData;
    c.s[X86::SS] = kStack;
    c.r[X86::SP] = kStackTop;
    c.r[X86::BX] = ds::kScriptBlock;
    c.r[X86::SI] = ds::kScriptBlock;
    c.host = [&](X86& m, std::uint16_t entry) {
        if (entry != kSpaceEntry) {
            throw CpuError("script called the game at a wrong place");
        }
        // As the original, every command first notes where it was called
        // from: the place a yielding command goes on from (some step on).
        set16(ds::kResume, m.read16(m.s[X86::SS], m.r[X86::SP]));
        ScriptCall call{m, m.arg(0)};
        std::uint32_t result = 0;
        const auto flow = host(call, result);
        m.r[X86::AX] = static_cast<std::uint16_t>(result);
        m.r[X86::DX] = static_cast<std::uint16_t>(result >> 16);
        if (flow == Flow::Yield) {
            m.stopRequested = true;
        }
    };
    struct Clear {
        X86& c;
        ~Clear() {
            c.host = nullptr;
            c.stopRequested = false;
        }
    } clear{c};
    c.run(kSpace, offset);
}

void Machine::loadRoomCode(std::uint16_t seg, std::span<const std::uint8_t> file, bool room) {
    if (file.size() < 0x30 || file.size() > 0xFFF0) {
        throw FormatError("room code of a wrong size");
    }
    Reader r(file);
    r.seek(6);
    const auto version = r.u16();
    const bool ok = room ? (version == 0x111 || version == 0x112) : (version == 0x211 || version == 0x212);
    if (!ok) {
        throw FormatError(room ? "not a room's code" : "not a mission's code");
    }
    std::uint8_t* base = cpu.memory() + X86::linear(seg, 0);
    std::fill_n(base, 0x10000, std::uint8_t{0});
    std::copy(file.begin(), file.end(), base);
    cpu.write16(seg, 0, kRoomEntry);
    cpu.write16(seg, 2, X86::kHostSegment);
    cpu.write16(seg, 4, ds::kScriptBlock);
}

void Machine::runHandler(std::uint16_t seg, std::uint16_t off, const std::uint8_t event[4], const RoomHost& host) {
    if (depth_ > 8) {
        throw CpuError("handlers nest too deep");
    }
    X86& c = cpu;
    const auto savedR = c.r;
    const auto savedS = c.s;
    const auto savedIp = c.ip;
    const auto savedFlags = c.flags;
    auto savedHost = c.host;
    set16(kEventBytes, event[0]);
    set16(static_cast<std::uint16_t>(kEventBytes + 2), event[1]);
    set16(static_cast<std::uint16_t>(kEventBytes + 4), event[2]);
    if (depth_ == 0) {
        c.s[X86::SS] = kStack;
        c.r[X86::SP] = kStackTop;
        c.r[X86::BP] = kStackTop;
    }
    c.s[X86::DS] = kData;
    c.s[X86::ES] = kData;
    c.r[X86::BX] = ds::kScriptBlock;
    c.r[X86::SI] = ds::kScriptBlock;
    c.host = [&](X86& m, std::uint16_t entry) {
        if (entry != kRoomEntry) {
            throw CpuError("room code called the game at a wrong place");
        }
        ScriptCall call{m, m.arg(0)};
        const std::uint32_t result = host(call);
        m.r[X86::AX] = static_cast<std::uint16_t>(result);
        m.r[X86::DX] = static_cast<std::uint16_t>(result >> 16);
    };
    struct Restore {
        Machine& m;
        decltype(savedR) r;
        decltype(savedS) s;
        std::uint16_t ip;
        decltype(savedFlags) flags;
        decltype(X86::host) hostFn;
        ~Restore() {
            --m.depth_;
            m.cpu.r = r;
            m.cpu.s = s;
            m.cpu.ip = ip;
            m.cpu.flags = flags;
            m.cpu.host = std::move(hostFn);
            m.cpu.stopRequested = false;
        }
    } restore{*this, savedR, savedS, savedIp, savedFlags, std::move(savedHost)};
    ++depth_;
    c.callFar(seg, off);
}

} // namespace helmsman::sequel
