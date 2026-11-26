// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small interpreter for 16-bit x86 code, as the 8086 and 80186 run it in
// real mode. Both games keep the actions of each room in the room's file
// (.RDF) as short pieces of such code, which call back into the game for
// everything they do. Helmsman runs those pieces here; the calls back land
// in Helmsman's own code. See docs/formats/room.md.
//
// Every memory access wraps inside the interpreter's own memory, so no code
// can reach outside it, and a run stops after a set number of steps.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace helmsman {

class CpuError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class X86 {
public:
    static constexpr std::uint32_t kMemorySize = 1u << 20; // a megabyte, as in real mode
    // Far calls to this segment do not run code: they call the host.
    static constexpr std::uint16_t kHostSegment = 0xFFFF;

    enum Reg16 : int { AX, CX, DX, BX, SP, BP, SI, DI };
    enum Seg : int { ES, CS, SS, DS };

    X86();

    // Registers.
    std::array<std::uint16_t, 8> r{};
    std::array<std::uint16_t, 4> s{};
    std::uint16_t ip = 0;
    std::uint16_t flags = 0x0002;

    // Memory.
    std::uint8_t read8(std::uint16_t seg, std::uint16_t off) const { return mem_[linear(seg, off)]; }
    std::uint16_t read16(std::uint16_t seg, std::uint16_t off) const {
        return static_cast<std::uint16_t>(read8(seg, off) | (read8(seg, static_cast<std::uint16_t>(off + 1)) << 8));
    }
    void write8(std::uint16_t seg, std::uint16_t off, std::uint8_t v) { mem_[linear(seg, off)] = v; }
    void write16(std::uint16_t seg, std::uint16_t off, std::uint16_t v) {
        write8(seg, off, static_cast<std::uint8_t>(v));
        write8(seg, static_cast<std::uint16_t>(off + 1), static_cast<std::uint8_t>(v >> 8));
    }
    std::uint8_t* memory() { return mem_.data(); }
    const std::uint8_t* memory() const { return mem_.data(); }
    static std::uint32_t linear(std::uint16_t seg, std::uint16_t off) {
        return ((std::uint32_t{seg} << 4) + off) & (kMemorySize - 1);
    }

    void push(std::uint16_t v) {
        r[SP] = static_cast<std::uint16_t>(r[SP] - 2);
        write16(s[SS], r[SP], v);
    }
    std::uint16_t pop() {
        const std::uint16_t v = read16(s[SS], r[SP]);
        r[SP] = static_cast<std::uint16_t>(r[SP] + 2);
        return v;
    }

    // The host's side of a far call into kHostSegment: called with the
    // offset called (the host's function number) and the machine, with the
    // caller's arguments on the stack above the return address (the first
    // at SS:SP+4). It returns with AX (and DX) set; the interpreter then
    // returns to the caller as a RETF would.
    std::function<void(X86&, std::uint16_t)> host;

    // Calls code at seg:off as a far procedure and runs it until it returns.
    // Throws CpuError on an instruction it does not know, or after
    // `maxSteps` instructions.
    void callFar(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps = 5'000'000);
    // Runs code from seg:off until the host sets stopRequested. For code
    // that never returns, such as the sequel's space scripts.
    void run(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps = 5'000'000);
    // Calls code at seg:off as a near procedure (in CS = seg).
    void callNear(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps = 5'000'000);

    // The argument at word `index` of a host call (0 the first).
    std::uint16_t arg(int index) const {
        return read16(s[SS], static_cast<std::uint16_t>(r[SP] + 4 + 2 * index));
    }

    // Flags.
    static constexpr std::uint16_t CF = 0x0001;
    static constexpr std::uint16_t PF = 0x0004;
    static constexpr std::uint16_t AF = 0x0010;
    static constexpr std::uint16_t ZF = 0x0040;
    static constexpr std::uint16_t SF = 0x0080;
    static constexpr std::uint16_t TF = 0x0100;
    static constexpr std::uint16_t IF = 0x0200;
    static constexpr std::uint16_t DF = 0x0400;
    static constexpr std::uint16_t OF = 0x0800;

    // Runs one instruction.
    void step();

    // Set to stop the run at the next instruction (for the host).
    bool stopRequested = false;

private:
    // Decoding.
    std::uint8_t fetch8() {
        const std::uint8_t v = read8(s[CS], ip);
        ip = static_cast<std::uint16_t>(ip + 1);
        return v;
    }
    std::uint16_t fetch16() {
        const std::uint16_t v = read16(s[CS], ip);
        ip = static_cast<std::uint16_t>(ip + 2);
        return v;
    }

    struct ModRM {
        int mod = 0;
        int reg = 0;
        int rm = 0;
        std::uint16_t seg = 0;
        std::uint16_t off = 0;
    };
    ModRM modrm();
    std::uint8_t rm8(const ModRM& m) const;
    std::uint16_t rm16(const ModRM& m) const;
    void setRm8(const ModRM& m, std::uint8_t v);
    void setRm16(const ModRM& m, std::uint16_t v);
    std::uint8_t reg8(int i) const;
    void setReg8(int i, std::uint8_t v);

    // Arithmetic with flags.
    std::uint32_t alu(int op, std::uint32_t a, std::uint32_t b, bool word);
    void setLogicFlags(std::uint32_t v, bool word);
    void setSZP(std::uint32_t v, bool word);
    std::uint32_t shift(int op, std::uint32_t v, unsigned count, bool word);
    std::uint32_t incDec(std::uint32_t v, bool dec, bool word);
    void group3(const ModRM& m, bool word);
    bool condition(int code) const;
    void stringOp(std::uint8_t op, int rep);
    void hostCall();
    void farReturn(std::uint16_t extra);

    bool flag(std::uint16_t f) const { return (flags & f) != 0; }
    void setFlag(std::uint16_t f, bool on) { flags = static_cast<std::uint16_t>(on ? (flags | f) : (flags & ~f)); }

    int segOverride_ = -1;
    std::vector<std::uint8_t> mem_;
};

} // namespace helmsman
