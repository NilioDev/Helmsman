// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "helmsman/cpu/x86.hpp"

#include <vector>

using namespace helmsman;

namespace {

void load(X86& cpu, std::uint16_t seg, const std::vector<std::uint8_t>& code) {
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.write8(seg, static_cast<std::uint16_t>(i), code[i]);
    }
}

} // namespace

HLM_TEST("x86: arithmetic, loops and a near call") {
    X86 cpu;
    cpu.s[X86::SS] = 0x9000;
    cpu.r[X86::SP] = 0xFFFE;
    // mov cx,5; xor ax,ax; l: add ax,cx; loop l; call f; retf; f: shl ax,1; ret
    load(cpu, 0x1000, {0xB9, 0x05, 0x00, 0x31, 0xC0, 0x01, 0xC8, 0xE2, 0xFC, 0xE8, 0x01, 0x00, 0xCB, 0xD1, 0xE0, 0xC3});
    cpu.callFar(0x1000, 0);
    HLM_CHECK(cpu.r[X86::AX] == 30);
    HLM_CHECK(cpu.r[X86::SP] == 0xFFFE);
}

HLM_TEST("x86: a far call to the host passes its arguments") {
    X86 cpu;
    cpu.s[X86::SS] = 0x9000;
    cpu.r[X86::SP] = 0xFFFE;
    // The room code's way: push 7; push 0x15; call far cs:[0]; add sp,4; retf
    load(cpu, 0x1000, {0x00, 0x00, 0xFF, 0xFF, 0x6A, 0x07, 0x6A, 0x15, 0x2E, 0xFF, 0x1E, 0x00, 0x00, 0x83, 0xC4, 0x04, 0xCB});
    int function = -1;
    int a0 = -1;
    int a1 = -1;
    cpu.host = [&](X86& c, std::uint16_t f) {
        function = f;
        a0 = c.arg(0);
        a1 = c.arg(1);
        c.r[X86::AX] = 42;
    };
    cpu.callFar(0x1000, 4);
    HLM_CHECK(function == 0);
    HLM_CHECK(a0 == 0x15 && a1 == 7);
    HLM_CHECK(cpu.r[X86::AX] == 42);
    HLM_CHECK(cpu.r[X86::SP] == 0xFFFE);
}

HLM_TEST("x86: flags of compares and conditional jumps") {
    X86 cpu;
    cpu.s[X86::SS] = 0x9000;
    cpu.r[X86::SP] = 0xFFFE;
    // mov ax,-1; cmp ax,1; jl yes; mov bx,0; retf; yes: mov bx,1; retf
    load(cpu, 0x1000, {0xB8, 0xFF, 0xFF, 0x3D, 0x01, 0x00, 0x7C, 0x04, 0x31, 0xDB, 0xCB, 0x90, 0xBB, 0x01, 0x00, 0xCB});
    cpu.callFar(0x1000, 0);
    HLM_CHECK(cpu.r[X86::BX] == 1);
}

HLM_TEST("x86: an unknown instruction throws") {
    X86 cpu;
    cpu.s[X86::SS] = 0x9000;
    cpu.r[X86::SP] = 0xFFFE;
    load(cpu, 0x1000, {0x0F, 0x0B});
    HLM_CHECK_THROWS(cpu.callFar(0x1000, 0));
}
