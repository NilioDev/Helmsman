// SPDX-License-Identifier: GPL-3.0-or-later
//
// A DOS program file, opened only to read tables the game keeps inside it
// (never to run it): the part that is loaded into memory, unpacked first
// when the program was stored packed the way Microsoft's linker could pack
// it ("EXEPACK"). See docs/formats/program.md.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace helmsman {

// What DOS would load into memory, as it stands before any relocation.
// Throws FormatError when the file is not a DOS program or its packed data
// does not unpack cleanly.
std::vector<std::uint8_t> programImage(std::span<const std::uint8_t> file);

} // namespace helmsman
