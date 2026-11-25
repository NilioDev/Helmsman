// SPDX-License-Identifier: GPL-3.0-or-later
//
// What lasts for a whole game of the first game: the player's files, the
// pictures, the font, what the crew carries, and the clock the game runs
// by (18.2 ticks a second, as the PC's timer).
#pragma once

#include "helmsman/away/assets.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/data/game_source.hpp"
#include "helmsman/engine/platform.hpp"
#include "helmsman/gfx/font.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace helmsman::away {

// The things the crew can carry, numbered from 0x40 as the rooms know them.
inline constexpr int kFirstItem = 0x40;
inline constexpr int kItemCount = 73;

struct ItemInfo {
    const char* picture;  // its picture (in the inventory)
    int description;      // its line in GROUND.TXT
    bool startsWith;      // carried from the start
};
const ItemInfo& itemInfo(int item);

class Session {
public:
    Session(Platform& platform, GameSource& source);

    Platform& platform;
    GameSource& source;
    Archive archive;
    Assets assets;
    Font font;

    std::array<bool, kItemCount> carried{};
    bool subtitles = true;

    // The game clock in ticks since the session started.
    std::uint64_t ticks() const;
    // Waits for the next tick, letting the app show frames meanwhile.
    void waitTick();

    // Plays a voice ("DEM0\DEM0_016") or a sound effect by its name.
    void playSpeech(std::string_view id);
    bool speaking() const;
    void stopSpeech();
    void playEffect(std::string_view name, bool loop = false);
    void stopEffects();

    // A line of a text file in the archive (lines end in a zero byte).
    std::string textLine(std::string_view file, int index);

private:
    std::uint64_t startMillis_ = 0;
    std::uint64_t lastTick_ = 0;
};

// The speaker's voice file and the words of a text as the rooms give it:
// "#DEM0\DEM0_016#Words..." (the voice part may be missing).
struct SpokenText {
    std::string voice;
    std::string words;
};
SpokenText splitSpoken(std::string_view text);

} // namespace helmsman::away
