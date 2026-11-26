// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's scripts and the values they share with the game. The
// scripts are 16-bit x86 code (in .RDF files) that reads and writes the
// original program's data segment directly, so Helmsman keeps a copy of
// that segment, laid out the same way, inside its own interpreter. Saved
// games are pieces of the same segment. See docs/rules/sequel-scripts.md.
#pragma once

#include "helmsman/cpu/x86.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace helmsman::sequel {

// Places in the data segment (`DS:xxxx` in the docs).
namespace ds {
inline constexpr std::uint16_t kScriptBlock = 0xC202; // the scripts' own 0x12A bytes
inline constexpr std::uint16_t kState = 0x6E0A;       // the game state
inline constexpr std::uint16_t kResume = 0x5EBA;      // where the space script goes on
inline constexpr std::uint16_t kEpisodeName = 0xC83A;
inline constexpr std::uint16_t kEpisode = 0x2636;
inline constexpr std::uint16_t kActors = 0x73D8; // 32 x 0x8A
inline constexpr std::uint16_t kActorSize = 0x8A;
inline constexpr std::uint16_t kCombatLevel = 0x00CC;
inline constexpr std::uint16_t kDestination = 0xC22A; // where the script wants us to go
inline constexpr std::uint16_t kSystem = 0xC228;      // the star system we are at
inline constexpr std::uint16_t kRoom = 0x11D6;        // the room an away mission starts in
inline constexpr std::uint16_t kSpawn = 0x11D8;       // and where in it
} // namespace ds

// One call from script code into the game.
struct ScriptCall {
    X86& cpu;
    int function = 0;
    // The arguments after the function number (0 the first).
    std::uint16_t arg(int i) const { return cpu.arg(i + 1); }
    std::int16_t sarg(int i) const { return static_cast<std::int16_t>(arg(i)); }
};

class Machine {
public:
    static constexpr std::uint16_t kData = 0x1000;   // the data segment
    static constexpr std::uint16_t kSpace = 0x2000;  // the space script
    static constexpr std::uint16_t kRoom = 0x3000;   // a room's code
    static constexpr std::uint16_t kShared = 0x4000; // a mission's shared code
    static constexpr std::uint16_t kStack = 0x5000;
    static constexpr std::uint16_t kStackTop = 0xFFF0;
    // Where the far pointer at offset 0 of each script points: the host's
    // function numbers for each kind of script.
    static constexpr std::uint16_t kSpaceEntry = 0x0100;
    static constexpr std::uint16_t kRoomEntry = 0x0200;

    // Starts from the data segment as the program's file holds it (its
    // initial values). Throws FormatError when the file is not the
    // sequel's program.
    explicit Machine(std::span<const std::uint8_t> programFile);

    X86 cpu;

    std::uint8_t u8(std::uint16_t off) const { return cpu.read8(kData, off); }
    std::uint16_t u16(std::uint16_t off) const { return cpu.read16(kData, off); }
    std::int16_t s16(std::uint16_t off) const { return static_cast<std::int16_t>(u16(off)); }
    void set8(std::uint16_t off, std::uint8_t v) { cpu.write8(kData, off, v); }
    void set16(std::uint16_t off, std::uint16_t v) { cpu.write16(kData, off, v); }
    std::string text(std::uint16_t off, std::size_t max = 256) const;
    void setText(std::uint16_t off, const std::string& s, std::size_t size);
    // Bytes of a script's segment, bounds-checked to its 64 KB.
    std::string scriptText(std::uint16_t seg, std::uint16_t off, std::size_t max = 1024) const;

    // Reads a saved game (or SAVEGAME.NEW) into the data segment. Throws
    // FormatError when it is damaged.
    void loadSaved(std::span<const std::uint8_t> bytes);

    // Loads an episode's space script. Throws FormatError.
    void loadSpaceScript(std::span<const std::uint8_t> file);
    bool hasSpaceScript() const { return spaceSize_ != 0; }
    std::uint16_t spaceWord(std::uint16_t off) const { return cpu.read16(kSpace, off); }

    // What a command does to the running script.
    enum class Flow { Go, Yield };
    // Runs the space script from `offset` until a command yields. The
    // host answers each command; it returns Yield to leave the script
    // there. Before each command the resume point (DS:5EBA) is set to the
    // return address of the call; the host may move it on.
    using Host = std::function<Flow(ScriptCall&, std::uint32_t& result)>;
    void runSpace(std::uint16_t offset, const Host& host);

private:
    std::size_t spaceSize_ = 0;
};

} // namespace helmsman::sequel
