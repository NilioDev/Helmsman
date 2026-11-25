// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the developer's own games in gamedata/ for tests that need real
// data: each game's CD image, or folder, there.
#pragma once

#include "helmsman/data/game_source.hpp"

#include <filesystem>
#include <memory>

namespace helmsman::test {

inline std::unique_ptr<GameSource> openGame(GameId game) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(HELMSMAN_GAMEDATA_DIR, ec)) {
        try {
            auto source = GameSource::open(entry.path());
            if (source && source->game() == game) {
                return source;
            }
        } catch (const std::exception&) {
        }
    }
    return nullptr;
}

} // namespace helmsman::test
