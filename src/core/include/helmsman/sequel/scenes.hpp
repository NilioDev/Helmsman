// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's scenes: films, painted animations and the little
// scripts (.DEM) that string them together with sounds, such as the
// opening titles. See docs/rules/sequel-scenes.md.
#pragma once

#include "helmsman/data/archive.hpp"
#include "helmsman/data/game_source.hpp"
#include "helmsman/data/sound_bank.hpp"
#include "helmsman/engine/platform.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helmsman::sequel {

// The files the scenes need from the player's copy.
class Assets {
public:
    explicit Assets(GameSource& source);

    GameSource& source() { return source_; }
    const Archive& data() const { return data_; }
    // A file of the UNCOMP folder (films and painted animations), such as
    // "LOGO.ANM".
    std::optional<std::vector<std::uint8_t>> uncompressed(std::string_view name);
    // A sound by name from the shared bank, or nothing.
    std::optional<PlainSound> sound(std::string_view name) const;

private:
    GameSource& source_;
    Archive data_;
    SoundBank sounds_;
};

// Plays a film (.MVE) in the middle of the screen. Returns false when the
// player skipped it.
bool playFilm(Platform& platform, std::vector<std::uint8_t> file);

// Plays a painted animation once, fading in and out. `onFrame` runs as
// each frame comes up and returns true to stop. Returns false when it was
// stopped.
bool playAnimation(Platform& platform, std::vector<std::uint8_t> file, const std::function<bool(int)>& onFrame);

// One record of a scene script.
struct SceneStep {
    int frame = -1; // -1: once nothing is playing
    int command = 0;
    int a = 0;
    int b = 0;
    std::string name;
};

// Reads a scene script. Throws FormatError.
std::vector<SceneStep> readSceneScript(std::span<const std::uint8_t> bytes);

// Plays a scene script such as "TITLE". A key or click skips to its end.
void playScene(Platform& platform, Assets& assets, std::string_view name);

} // namespace helmsman::sequel
