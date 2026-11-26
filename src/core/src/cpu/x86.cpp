// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/cpu/x86.hpp"

#include <cstdio>
#include <string>

namespace helmsman {

namespace {

// The return address a run starts with: when the code returns there, the
// run ends.
constexpr std::uint16_t kStopOffset = 0xFFF0;

bool parity(std::uint8_t v) {
    v ^= static_cast<std::uint8_t>(v >> 4);
    v ^= static_cast<std::uint8_t>(v >> 2);
    v ^= static_cast<std::uint8_t>(v >> 1);
    return (v & 1) == 0;
}

std::string hex(unsigned v) {
    char text[16];
    std::snprintf(text, sizeof text, "%X", v);
    return text;
}

} // namespace

X86::X86() : mem_(kMemorySize, 0) {
    // At the stop address: nothing; the run loop checks for it.
}

X86::ModRM X86::modrm() {
    const std::uint8_t b = fetch8();
    ModRM m;
    m.mod = b >> 6;
    m.reg = (b >> 3) & 7;
    m.rm = b & 7;
    if (m.mod == 3) {
        return m;
    }
    std::uint16_t off = 0;
    int defSeg = DS;
    switch (m.rm) {
    case 0: off = static_cast<std::uint16_t>(r[BX] + r[SI]); break;
    case 1: off = static_cast<std::uint16_t>(r[BX] + r[DI]); break;
    case 2: off = static_cast<std::uint16_t>(r[BP] + r[SI]); defSeg = SS; break;
    case 3: off = static_cast<std::uint16_t>(r[BP] + r[DI]); defSeg = SS; break;
    case 4: off = r[SI]; break;
    case 5: off = r[DI]; break;
    case 6:
        if (m.mod == 0) {
            off = fetch16();
        } else {
            off = r[BP];
            defSeg = SS;
        }
        break;
    case 7: off = r[BX]; break;
    default: break;
    }
    if (m.mod == 1) {
        off = static_cast<std::uint16_t>(off + static_cast<std::int8_t>(fetch8()));
    } else if (m.mod == 2) {
        off = static_cast<std::uint16_t>(off + fetch16());
    }
    m.seg = s[segOverride_ >= 0 ? segOverride_ : defSeg];
    m.off = off;
    return m;
}

std::uint8_t X86::reg8(int i) const {
    return i < 4 ? static_cast<std::uint8_t>(r[i]) : static_cast<std::uint8_t>(r[i - 4] >> 8);
}

void X86::setReg8(int i, std::uint8_t v) {
    if (i < 4) {
        r[i] = static_cast<std::uint16_t>((r[i] & 0xFF00) | v);
    } else {
        r[i - 4] = static_cast<std::uint16_t>((r[i - 4] & 0x00FF) | (v << 8));
    }
}

std::uint8_t X86::rm8(const ModRM& m) const { return m.mod == 3 ? reg8(m.rm) : read8(m.seg, m.off); }
std::uint16_t X86::rm16(const ModRM& m) const { return m.mod == 3 ? r[m.rm] : read16(m.seg, m.off); }
void X86::setRm8(const ModRM& m, std::uint8_t v) {
    if (m.mod == 3) {
        setReg8(m.rm, v);
    } else {
        write8(m.seg, m.off, v);
    }
}
void X86::setRm16(const ModRM& m, std::uint16_t v) {
    if (m.mod == 3) {
        r[m.rm] = v;
    } else {
        write16(m.seg, m.off, v);
    }
}

void X86::setSZP(std::uint32_t v, bool word) {
    const std::uint32_t mask = word ? 0xFFFF : 0xFF;
    const std::uint32_t sign = word ? 0x8000 : 0x80;
    setFlag(ZF, (v & mask) == 0);
    setFlag(SF, (v & sign) != 0);
    setFlag(PF, parity(static_cast<std::uint8_t>(v)));
}

void X86::setLogicFlags(std::uint32_t v, bool word) {
    setSZP(v, word);
    setFlag(CF, false);
    setFlag(OF, false);
    setFlag(AF, false);
}

// op: 0 add, 1 or, 2 adc, 3 sbb, 4 and, 5 sub, 6 xor, 7 cmp.
std::uint32_t X86::alu(int op, std::uint32_t a, std::uint32_t b, bool word) {
    const std::uint32_t mask = word ? 0xFFFF : 0xFF;
    const std::uint32_t sign = word ? 0x8000 : 0x80;
    a &= mask;
    b &= mask;
    std::uint32_t res = 0;
    switch (op) {
    case 0:
    case 2: {
        const std::uint32_t carry = op == 2 && flag(CF) ? 1 : 0;
        res = a + b + carry;
        setFlag(CF, res > mask);
        setFlag(OF, ((a ^ res) & (b ^ res) & sign) != 0);
        setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
        setSZP(res, word);
        return res & mask;
    }
    case 3:
    case 5:
    case 7: {
        const std::uint32_t borrow = op == 3 && flag(CF) ? 1 : 0;
        res = a - b - borrow;
        setFlag(CF, a < b + borrow);
        setFlag(OF, ((a ^ b) & (a ^ res) & sign) != 0);
        setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
        setSZP(res, word);
        return op == 7 ? a : (res & mask);
    }
    case 1: res = a | b; break;
    case 4: res = a & b; break;
    case 6: res = a ^ b; break;
    default: break;
    }
    setLogicFlags(res, word);
    return res & mask;
}

std::uint32_t X86::incDec(std::uint32_t v, bool dec, bool word) {
    const bool carry = flag(CF);
    const std::uint32_t res = alu(dec ? 5 : 0, v, 1, word);
    setFlag(CF, carry);
    return res;
}

// op: 0 rol, 1 ror, 2 rcl, 3 rcr, 4 shl, 5 shr, 6 sal, 7 sar.
std::uint32_t X86::shift(int op, std::uint32_t v, unsigned count, bool word) {
    const unsigned bits = word ? 16 : 8;
    const std::uint32_t mask = word ? 0xFFFF : 0xFF;
    const std::uint32_t sign = word ? 0x8000 : 0x80;
    count &= 0x1F;
    if (count == 0) {
        return v & mask;
    }
    v &= mask;
    switch (op) {
    case 0:
        for (unsigned i = 0; i < count; ++i) {
            const bool top = (v & sign) != 0;
            v = ((v << 1) | (top ? 1 : 0)) & mask;
            setFlag(CF, top);
        }
        setFlag(OF, ((v & sign) != 0) != flag(CF));
        return v;
    case 1:
        for (unsigned i = 0; i < count; ++i) {
            const bool low = (v & 1) != 0;
            v = (v >> 1) | (low ? sign : 0);
            setFlag(CF, low);
        }
        setFlag(OF, ((v ^ (v << 1)) & sign) != 0);
        return v;
    case 2:
        for (unsigned i = 0; i < count; ++i) {
            const bool top = (v & sign) != 0;
            v = ((v << 1) | (flag(CF) ? 1 : 0)) & mask;
            setFlag(CF, top);
        }
        setFlag(OF, ((v & sign) != 0) != flag(CF));
        return v;
    case 3:
        for (unsigned i = 0; i < count; ++i) {
            const bool low = (v & 1) != 0;
            v = (v >> 1) | (flag(CF) ? sign : 0);
            setFlag(CF, low);
        }
        setFlag(OF, ((v ^ (v << 1)) & sign) != 0);
        return v;
    case 4:
    case 6: {
        const std::uint32_t res = count > bits ? 0 : (v << count);
        setFlag(CF, count <= bits && (res & (mask + 1)) != 0);
        setFlag(OF, ((res & sign) != 0) != flag(CF));
        setSZP(res, word);
        return res & mask;
    }
    case 5: {
        setFlag(CF, count <= bits && ((v >> (count - 1)) & 1) != 0);
        setFlag(OF, (v & sign) != 0);
        const std::uint32_t res = count >= bits ? 0 : (v >> count);
        setSZP(res, word);
        return res & mask;
    }
    case 7: {
        std::int32_t sv = word ? static_cast<std::int16_t>(v) : static_cast<std::int8_t>(v);
        const unsigned n = count >= bits ? bits - 1 : count;
        setFlag(CF, ((sv >> (n - 1 + (count >= bits ? 1 : 0))) & 1) != 0);
        sv >>= n;
        setFlag(OF, false);
        setSZP(static_cast<std::uint32_t>(sv), word);
        return static_cast<std::uint32_t>(sv) & mask;
    }
    default: return v;
    }
}

void X86::group3(const ModRM& m, bool word) {
    switch (m.reg) {
    case 0:
    case 1: {
        const std::uint32_t v = word ? rm16(m) : rm8(m);
        const std::uint32_t imm = word ? fetch16() : fetch8();
        setLogicFlags(v & imm, word);
        return;
    }
    case 2:
        if (word) {
            setRm16(m, static_cast<std::uint16_t>(~rm16(m)));
        } else {
            setRm8(m, static_cast<std::uint8_t>(~rm8(m)));
        }
        return;
    case 3: {
        const std::uint32_t v = word ? rm16(m) : rm8(m);
        const std::uint32_t res = alu(5, 0, v, word);
        setFlag(CF, v != 0);
        if (word) {
            setRm16(m, static_cast<std::uint16_t>(res));
        } else {
            setRm8(m, static_cast<std::uint8_t>(res));
        }
        return;
    }
    case 4: // mul
        if (word) {
            const std::uint32_t res = std::uint32_t{r[AX]} * rm16(m);
            r[AX] = static_cast<std::uint16_t>(res);
            r[DX] = static_cast<std::uint16_t>(res >> 16);
            setFlag(CF, r[DX] != 0);
            setFlag(OF, r[DX] != 0);
        } else {
            const std::uint32_t res = std::uint32_t{static_cast<std::uint8_t>(r[AX])} * rm8(m);
            r[AX] = static_cast<std::uint16_t>(res);
            setFlag(CF, (res & 0xFF00) != 0);
            setFlag(OF, (res & 0xFF00) != 0);
        }
        return;
    case 5: // imul
        if (word) {
            const std::int32_t res =
                std::int32_t{static_cast<std::int16_t>(r[AX])} * static_cast<std::int16_t>(rm16(m));
            r[AX] = static_cast<std::uint16_t>(res);
            r[DX] = static_cast<std::uint16_t>(static_cast<std::uint32_t>(res) >> 16);
            const bool over = res != static_cast<std::int16_t>(res);
            setFlag(CF, over);
            setFlag(OF, over);
        } else {
            const std::int32_t res =
                std::int32_t{static_cast<std::int8_t>(r[AX])} * static_cast<std::int8_t>(rm8(m));
            r[AX] = static_cast<std::uint16_t>(res);
            const bool over = res != static_cast<std::int8_t>(res);
            setFlag(CF, over);
            setFlag(OF, over);
        }
        return;
    case 6: // div
        if (word) {
            const std::uint32_t num = (std::uint32_t{r[DX]} << 16) | r[AX];
            const std::uint32_t den = rm16(m);
            if (den == 0 || num / den > 0xFFFF) {
                throw CpuError("division overflow");
            }
            r[AX] = static_cast<std::uint16_t>(num / den);
            r[DX] = static_cast<std::uint16_t>(num % den);
        } else {
            const std::uint32_t num = r[AX];
            const std::uint32_t den = rm8(m);
            if (den == 0 || num / den > 0xFF) {
                throw CpuError("division overflow");
            }
            r[AX] = static_cast<std::uint16_t>(((num % den) << 8) | (num / den));
        }
        return;
    case 7: // idiv
        if (word) {
            const std::int32_t num = static_cast<std::int32_t>((std::uint32_t{r[DX]} << 16) | r[AX]);
            const std::int32_t den = static_cast<std::int16_t>(rm16(m));
            if (den == 0) {
                throw CpuError("division by zero");
            }
            const std::int32_t q = num / den;
            if (q > 32767 || q < -32768) {
                throw CpuError("division overflow");
            }
            r[AX] = static_cast<std::uint16_t>(q);
            r[DX] = static_cast<std::uint16_t>(num % den);
        } else {
            const std::int32_t num = static_cast<std::int16_t>(r[AX]);
            const std::int32_t den = static_cast<std::int8_t>(rm8(m));
            if (den == 0) {
                throw CpuError("division by zero");
            }
            const std::int32_t q = num / den;
            if (q > 127 || q < -128) {
                throw CpuError("division overflow");
            }
            r[AX] = static_cast<std::uint16_t>(((num % den) & 0xFF) << 8 | (q & 0xFF));
        }
        return;
    default: return;
    }
}

bool X86::condition(int code) const {
    bool v = false;
    switch (code >> 1) {
    case 0: v = flag(OF); break;
    case 1: v = flag(CF); break;
    case 2: v = flag(ZF); break;
    case 3: v = flag(CF) || flag(ZF); break;
    case 4: v = flag(SF); break;
    case 5: v = flag(PF); break;
    case 6: v = flag(SF) != flag(OF); break;
    case 7: v = flag(ZF) || (flag(SF) != flag(OF)); break;
    default: break;
    }
    return (code & 1) ? !v : v;
}

void X86::stringOp(std::uint8_t op, int rep) {
    const bool word = (op & 1) != 0;
    const int size = word ? 2 : 1;
    const int delta = flag(DF) ? -size : size;
    const std::uint16_t srcSeg = s[segOverride_ >= 0 ? segOverride_ : DS];
    auto once = [&]() {
        switch (op & 0xFE) {
        case 0xA4: // movs
            if (word) {
                write16(s[ES], r[DI], read16(srcSeg, r[SI]));
            } else {
                write8(s[ES], r[DI], read8(srcSeg, r[SI]));
            }
            r[SI] = static_cast<std::uint16_t>(r[SI] + delta);
            r[DI] = static_cast<std::uint16_t>(r[DI] + delta);
            break;
        case 0xA6: // cmps
            alu(7, word ? read16(srcSeg, r[SI]) : read8(srcSeg, r[SI]),
                word ? read16(s[ES], r[DI]) : read8(s[ES], r[DI]), word);
            r[SI] = static_cast<std::uint16_t>(r[SI] + delta);
            r[DI] = static_cast<std::uint16_t>(r[DI] + delta);
            break;
        case 0xAA: // stos
            if (word) {
                write16(s[ES], r[DI], r[AX]);
            } else {
                write8(s[ES], r[DI], static_cast<std::uint8_t>(r[AX]));
            }
            r[DI] = static_cast<std::uint16_t>(r[DI] + delta);
            break;
        case 0xAC: // lods
            if (word) {
                r[AX] = read16(srcSeg, r[SI]);
            } else {
                setReg8(0, read8(srcSeg, r[SI]));
            }
            r[SI] = static_cast<std::uint16_t>(r[SI] + delta);
            break;
        case 0xAE: // scas
            alu(7, word ? r[AX] : (r[AX] & 0xFF), word ? read16(s[ES], r[DI]) : read8(s[ES], r[DI]), word);
            r[DI] = static_cast<std::uint16_t>(r[DI] + delta);
            break;
        default: break;
        }
    };
    if (rep == 0) {
        once();
        return;
    }
    const bool compares = (op & 0xFE) == 0xA6 || (op & 0xFE) == 0xAE;
    while (r[CX] != 0) {
        once();
        r[CX] = static_cast<std::uint16_t>(r[CX] - 1);
        if (compares && ((rep == 0xF3) != flag(ZF))) {
            break;
        }
    }
}

void X86::farReturn(std::uint16_t extra) {
    ip = pop();
    s[CS] = pop();
    r[SP] = static_cast<std::uint16_t>(r[SP] + extra);
}

void X86::hostCall() {
    // CS:IP is in the host segment: the offset names the function. The
    // return address is on the stack, as the far call left it.
    const std::uint16_t function = ip;
    if (host) {
        host(*this, function);
    }
    farReturn(0);
}

void X86::step() {
    if (s[CS] == kHostSegment) {
        hostCall();
        return;
    }
    segOverride_ = -1;
    int rep = 0;
    std::uint8_t op = 0;
    const std::uint16_t startIp = ip;
    // Prefixes.
    for (;;) {
        op = fetch8();
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) {
            segOverride_ = (op >> 3) & 3;
        } else if (op == 0xF2 || op == 0xF3) {
            rep = op;
        } else if (op == 0xF0) {
            // lock: nothing to do
        } else {
            break;
        }
    }

    if (op < 0x40 && (op & 7) < 6) {
        // add/or/adc/sbb/and/sub/xor/cmp in their six forms.
        const int aluOp = op >> 3;
        const bool word = (op & 1) != 0;
        switch (op & 7) {
        case 0:
        case 1: {
            const ModRM m = modrm();
            const std::uint32_t a = word ? rm16(m) : rm8(m);
            const std::uint32_t b = word ? r[m.reg] : reg8(m.reg);
            const std::uint32_t res = alu(aluOp, a, b, word);
            if (aluOp != 7) {
                word ? setRm16(m, static_cast<std::uint16_t>(res)) : setRm8(m, static_cast<std::uint8_t>(res));
            }
            return;
        }
        case 2:
        case 3: {
            const ModRM m = modrm();
            const std::uint32_t a = word ? r[m.reg] : reg8(m.reg);
            const std::uint32_t b = word ? rm16(m) : rm8(m);
            const std::uint32_t res = alu(aluOp, a, b, word);
            if (aluOp != 7) {
                if (word) {
                    r[m.reg] = static_cast<std::uint16_t>(res);
                } else {
                    setReg8(m.reg, static_cast<std::uint8_t>(res));
                }
            }
            return;
        }
        case 4: {
            const std::uint32_t res = alu(aluOp, r[AX] & 0xFF, fetch8(), false);
            if (aluOp != 7) {
                setReg8(0, static_cast<std::uint8_t>(res));
            }
            return;
        }
        case 5: {
            const std::uint32_t res = alu(aluOp, r[AX], fetch16(), true);
            if (aluOp != 7) {
                r[AX] = static_cast<std::uint16_t>(res);
            }
            return;
        }
        default: break;
        }
    }

    switch (op) {
    case 0x06: push(s[ES]); return;
    case 0x07: s[ES] = pop(); return;
    case 0x0E: push(s[CS]); return;
    case 0x16: push(s[SS]); return;
    case 0x17: s[SS] = pop(); return;
    case 0x1E: push(s[DS]); return;
    case 0x1F: s[DS] = pop(); return;
    case 0x27: { // daa
        std::uint8_t al = static_cast<std::uint8_t>(r[AX]);
        const bool cf = flag(CF);
        if ((al & 0x0F) > 9 || flag(AF)) {
            al = static_cast<std::uint8_t>(al + 6);
            setFlag(AF, true);
        }
        if (al > 0x9F || cf) {
            al = static_cast<std::uint8_t>(al + 0x60);
            setFlag(CF, true);
        }
        setReg8(0, al);
        setSZP(al, false);
        return;
    }
    case 0x98: r[AX] = static_cast<std::uint16_t>(static_cast<std::int8_t>(r[AX])); return; // cbw
    case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xFFFF : 0; return;                              // cwd
    case 0x90: return;
    case 0x9C: push(flags); return;
    case 0x9D: flags = static_cast<std::uint16_t>((pop() & 0x0FD5) | 0x0002); return;
    case 0x9E: flags = static_cast<std::uint16_t>((flags & 0xFF00) | (r[AX] >> 8 & 0xD5) | 2); return; // sahf
    case 0x9F: setReg8(4, static_cast<std::uint8_t>(flags)); return;                                     // lahf
    case 0xF5: setFlag(CF, !flag(CF)); return;
    case 0xF8: setFlag(CF, false); return;
    case 0xF9: setFlag(CF, true); return;
    case 0xFA: setFlag(IF, false); return;
    case 0xFB: setFlag(IF, true); return;
    case 0xFC: setFlag(DF, false); return;
    case 0xFD: setFlag(DF, true); return;
    case 0xD7: setReg8(0, read8(s[segOverride_ >= 0 ? segOverride_ : DS], static_cast<std::uint16_t>(r[BX] + (r[AX] & 0xFF)))); return;
    default: break;
    }

    if (op >= 0x40 && op <= 0x47) {
        r[op & 7] = static_cast<std::uint16_t>(incDec(r[op & 7], false, true));
        return;
    }
    if (op >= 0x48 && op <= 0x4F) {
        r[op & 7] = static_cast<std::uint16_t>(incDec(r[op & 7], true, true));
        return;
    }
    if (op >= 0x50 && op <= 0x57) {
        // push sp pushes the value before the push on the 286 and later.
        const std::uint16_t v = r[op & 7];
        push(v);
        return;
    }
    if (op >= 0x58 && op <= 0x5F) {
        r[op & 7] = pop();
        return;
    }
    if (op >= 0x70 && op <= 0x7F) {
        const auto d = static_cast<std::int8_t>(fetch8());
        if (condition(op & 0x0F)) {
            ip = static_cast<std::uint16_t>(ip + d);
        }
        return;
    }
    if (op >= 0x91 && op <= 0x97) {
        std::swap(r[AX], r[op & 7]);
        return;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        setReg8(op & 7, fetch8());
        return;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        r[op & 7] = fetch16();
        return;
    }
    if (op >= 0xA4 && op <= 0xA7) {
        stringOp(op, rep);
        return;
    }
    if (op >= 0xAA && op <= 0xAF) {
        stringOp(op, rep);
        return;
    }

    switch (op) {
    case 0x60: { // pusha
        const std::uint16_t sp = r[SP];
        push(r[AX]);
        push(r[CX]);
        push(r[DX]);
        push(r[BX]);
        push(sp);
        push(r[BP]);
        push(r[SI]);
        push(r[DI]);
        return;
    }
    case 0x61: // popa
        r[DI] = pop();
        r[SI] = pop();
        r[BP] = pop();
        pop();
        r[BX] = pop();
        r[DX] = pop();
        r[CX] = pop();
        r[AX] = pop();
        return;
    case 0x68: push(fetch16()); return;
    case 0x6A: push(static_cast<std::uint16_t>(static_cast<std::int8_t>(fetch8()))); return;
    case 0x69:
    case 0x6B: {
        const ModRM m = modrm();
        const std::int32_t a = static_cast<std::int16_t>(rm16(m));
        const std::int32_t b =
            op == 0x69 ? static_cast<std::int16_t>(fetch16()) : static_cast<std::int8_t>(fetch8());
        const std::int32_t res = a * b;
        r[m.reg] = static_cast<std::uint16_t>(res);
        const bool over = res != static_cast<std::int16_t>(res);
        setFlag(CF, over);
        setFlag(OF, over);
        return;
    }
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83: {
        const ModRM m = modrm();
        const bool word = (op & 1) != 0;
        const std::uint32_t a = word ? rm16(m) : rm8(m);
        std::uint32_t b = 0;
        if (op == 0x81) {
            b = fetch16();
        } else if (op == 0x83) {
            b = static_cast<std::uint16_t>(static_cast<std::int8_t>(fetch8()));
        } else {
            b = fetch8();
        }
        const std::uint32_t res = alu(m.reg, a, b, word);
        if (m.reg != 7) {
            word ? setRm16(m, static_cast<std::uint16_t>(res)) : setRm8(m, static_cast<std::uint8_t>(res));
        }
        return;
    }
    case 0x84: {
        const ModRM m = modrm();
        setLogicFlags(rm8(m) & reg8(m.reg), false);
        return;
    }
    case 0x85: {
        const ModRM m = modrm();
        setLogicFlags(rm16(m) & r[m.reg], true);
        return;
    }
    case 0x86: {
        const ModRM m = modrm();
        const std::uint8_t a = rm8(m);
        setRm8(m, reg8(m.reg));
        setReg8(m.reg, a);
        return;
    }
    case 0x87: {
        const ModRM m = modrm();
        const std::uint16_t a = rm16(m);
        setRm16(m, r[m.reg]);
        r[m.reg] = a;
        return;
    }
    case 0x88: {
        const ModRM m = modrm();
        setRm8(m, reg8(m.reg));
        return;
    }
    case 0x89: {
        const ModRM m = modrm();
        setRm16(m, r[m.reg]);
        return;
    }
    case 0x8A: {
        const ModRM m = modrm();
        setReg8(m.reg, rm8(m));
        return;
    }
    case 0x8B: {
        const ModRM m = modrm();
        r[m.reg] = rm16(m);
        return;
    }
    case 0x8C: {
        const ModRM m = modrm();
        setRm16(m, s[m.reg & 3]);
        return;
    }
    case 0x8D: {
        const ModRM m = modrm();
        r[m.reg] = m.off;
        return;
    }
    case 0x8E: {
        const ModRM m = modrm();
        s[m.reg & 3] = rm16(m);
        return;
    }
    case 0x8F: {
        const ModRM m = modrm();
        setRm16(m, pop());
        return;
    }
    case 0x9A: {
        const std::uint16_t off = fetch16();
        const std::uint16_t seg = fetch16();
        push(s[CS]);
        push(ip);
        s[CS] = seg;
        ip = off;
        return;
    }
    case 0xA0: setReg8(0, read8(s[segOverride_ >= 0 ? segOverride_ : DS], fetch16())); return;
    case 0xA1: r[AX] = read16(s[segOverride_ >= 0 ? segOverride_ : DS], fetch16()); return;
    case 0xA2: write8(s[segOverride_ >= 0 ? segOverride_ : DS], fetch16(), static_cast<std::uint8_t>(r[AX])); return;
    case 0xA3: write16(s[segOverride_ >= 0 ? segOverride_ : DS], fetch16(), r[AX]); return;
    case 0xA8: setLogicFlags((r[AX] & 0xFF) & fetch8(), false); return;
    case 0xA9: setLogicFlags(r[AX] & fetch16(), true); return;
    case 0xC0:
    case 0xC1:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3: {
        const ModRM m = modrm();
        const bool word = (op & 1) != 0;
        unsigned count = 1;
        if (op == 0xC0 || op == 0xC1) {
            count = fetch8();
        } else if (op == 0xD2 || op == 0xD3) {
            count = r[CX] & 0xFF;
        }
        const std::uint32_t res = shift(m.reg, word ? rm16(m) : rm8(m), count, word);
        word ? setRm16(m, static_cast<std::uint16_t>(res)) : setRm8(m, static_cast<std::uint8_t>(res));
        return;
    }
    case 0xC2: {
        const std::uint16_t n = fetch16();
        ip = pop();
        r[SP] = static_cast<std::uint16_t>(r[SP] + n);
        return;
    }
    case 0xC3: ip = pop(); return;
    case 0xC4:
    case 0xC5: {
        const ModRM m = modrm();
        r[m.reg] = read16(m.seg, m.off);
        s[op == 0xC4 ? ES : DS] = read16(m.seg, static_cast<std::uint16_t>(m.off + 2));
        return;
    }
    case 0xC6: {
        const ModRM m = modrm();
        setRm8(m, fetch8());
        return;
    }
    case 0xC7: {
        const ModRM m = modrm();
        setRm16(m, fetch16());
        return;
    }
    case 0xC8: { // enter
        const std::uint16_t size = fetch16();
        const std::uint8_t level = fetch8() & 0x1F;
        push(r[BP]);
        const std::uint16_t frame = r[SP];
        for (int i = 1; i < level; ++i) {
            r[BP] = static_cast<std::uint16_t>(r[BP] - 2);
            push(read16(s[SS], r[BP]));
        }
        if (level > 0) {
            push(frame);
        }
        r[BP] = frame;
        r[SP] = static_cast<std::uint16_t>(r[SP] - size);
        return;
    }
    case 0xC9: // leave
        r[SP] = r[BP];
        r[BP] = pop();
        return;
    case 0xCA: farReturn(fetch16()); return;
    case 0xCB: farReturn(0); return;
    case 0xE0:
    case 0xE1:
    case 0xE2: {
        const auto d = static_cast<std::int8_t>(fetch8());
        r[CX] = static_cast<std::uint16_t>(r[CX] - 1);
        bool jump = r[CX] != 0;
        if (op == 0xE0) {
            jump = jump && !flag(ZF);
        } else if (op == 0xE1) {
            jump = jump && flag(ZF);
        }
        if (jump) {
            ip = static_cast<std::uint16_t>(ip + d);
        }
        return;
    }
    case 0xE3: {
        const auto d = static_cast<std::int8_t>(fetch8());
        if (r[CX] == 0) {
            ip = static_cast<std::uint16_t>(ip + d);
        }
        return;
    }
    case 0xE8: {
        const std::uint16_t d = fetch16();
        push(ip);
        ip = static_cast<std::uint16_t>(ip + d);
        return;
    }
    case 0xE9: {
        const std::uint16_t d = fetch16();
        ip = static_cast<std::uint16_t>(ip + d);
        return;
    }
    case 0xEA: {
        const std::uint16_t off = fetch16();
        s[CS] = fetch16();
        ip = off;
        return;
    }
    case 0xEB: {
        const auto d = static_cast<std::int8_t>(fetch8());
        ip = static_cast<std::uint16_t>(ip + d);
        return;
    }
    case 0xF6:
    case 0xF7: {
        const ModRM m = modrm();
        group3(m, op == 0xF7);
        return;
    }
    case 0xFE: {
        const ModRM m = modrm();
        if (m.reg > 1) {
            break;
        }
        setRm8(m, static_cast<std::uint8_t>(incDec(rm8(m), m.reg == 1, false)));
        return;
    }
    case 0xFF: {
        const ModRM m = modrm();
        switch (m.reg) {
        case 0: setRm16(m, static_cast<std::uint16_t>(incDec(rm16(m), false, true))); return;
        case 1: setRm16(m, static_cast<std::uint16_t>(incDec(rm16(m), true, true))); return;
        case 2: {
            const std::uint16_t to = rm16(m);
            push(ip);
            ip = to;
            return;
        }
        case 3: {
            const std::uint16_t off = read16(m.seg, m.off);
            const std::uint16_t seg = read16(m.seg, static_cast<std::uint16_t>(m.off + 2));
            push(s[CS]);
            push(ip);
            s[CS] = seg;
            ip = off;
            return;
        }
        case 4: ip = rm16(m); return;
        case 5: {
            const std::uint16_t off = read16(m.seg, m.off);
            s[CS] = read16(m.seg, static_cast<std::uint16_t>(m.off + 2));
            ip = off;
            return;
        }
        case 6: push(rm16(m)); return;
        default: break;
        }
        break;
    }
    default: break;
    }
    throw CpuError("unknown instruction " + hex(op) + " at " + hex(s[CS]) + ":" + hex(startIp));
}

void X86::callFar(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps) {
    // A return to the stop address ends the run.
    push(kHostSegment);
    push(kStopOffset);
    s[CS] = seg;
    ip = off;
    stopRequested = false;
    for (std::uint64_t n = 0; n < maxSteps; ++n) {
        if (s[CS] == kHostSegment && ip == kStopOffset) {
            return;
        }
        step();
        if (stopRequested) {
            return;
        }
    }
    throw CpuError("room code ran too long");
}

void X86::run(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps) {
    s[CS] = seg;
    ip = off;
    stopRequested = false;
    for (std::uint64_t n = 0; n < maxSteps; ++n) {
        if (s[CS] == kHostSegment && ip == kStopOffset) {
            throw CpuError("script code returned");
        }
        step();
        if (stopRequested) {
            return;
        }
    }
    throw CpuError("script code ran too long");
}

void X86::callNear(std::uint16_t seg, std::uint16_t off, std::uint64_t maxSteps) {
    // A near call from a far stub: push a far return to the stop address,
    // then a near return to a RETF placed just before the stop offset.
    push(kHostSegment);
    push(kStopOffset);
    s[CS] = seg;
    // The near return lands on a RETF placed at the top of the code segment.
    constexpr std::uint16_t kRetf = 0xFFFE;
    write8(seg, kRetf, 0xCB);
    push(kRetf);
    ip = off;
    stopRequested = false;
    for (std::uint64_t n = 0; n < maxSteps; ++n) {
        if (s[CS] == kHostSegment && ip == kStopOffset) {
            return;
        }
        step();
        if (stopRequested) {
            return;
        }
    }
    throw CpuError("room code ran too long");
}

} // namespace helmsman
