// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/away/menus.hpp"

#include "helmsman/engine/savegame.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <vector>

namespace helmsman::away {

namespace {

constexpr const char* kGame = "anniversary";

// The mission's number as the player knows it: the fifth and sixth
// missions' planets are one mission.
int missionNumber(const std::string& name) {
    static const char* const kOrder[] = {"DEMON", "TUG", "LOVE", "MUDD", "FEATHER", "TRIAL", "SINS", "VENG"};
    static const int kNumber[] = {1, 2, 3, 4, 5, 5, 6, 7};
    std::string up;
    for (const char c : name) {
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (int i = 0; i < 8; ++i) {
        if (up == kOrder[i]) {
            return kNumber[i];
        }
    }
    return 0;
}

std::string timeNow() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%d %b %H:%M", &local);
    return buf;
}

} // namespace

void saveGameMenu(Ui& ui, const std::string& mission, std::span<const std::uint8_t> payload) {
    Platform& p = ui.session.platform;
    const auto saves = listSaves(p.saveFolder, kGame);
    std::vector<std::string> lines = {"Save Game"};
    const bool room = static_cast<int>(saves.size()) < kMaxSaves;
    if (room) {
        lines.push_back("#GENE\\GENER023#Save new game.");
    }
    for (const auto& s : saves) {
        lines.push_back("Replace: " + s.description);
    }
    const int choice = textBox(ui, lines, 0x14, 0x14, 0xB0);
    if (choice < 0 || choice >= static_cast<int>(lines.size()) - 1) {
        return;
    }
    const int number = room && choice == 0 ? freeSaveNumber(p.saveFolder, kGame)
                                           : saves[static_cast<std::size_t>(choice - (room ? 1 : 0))].number;
    char description[64];
    std::snprintf(description, sizeof description, "Mission %d, %s", missionNumber(mission), timeNow().c_str());
    if (number < 0 || !writeSave(p.saveFolder, kGame, number, description, payload)) {
        textBox(ui, {"", "The game could not be saved."}, 0x14, 0x14, 0xB0);
    }
}

int loadGameMenu(Ui& ui) {
    Platform& p = ui.session.platform;
    const auto saves = listSaves(p.saveFolder, kGame);
    if (saves.empty()) {
        textBox(ui, {"", "#GENE\\GENERJ01#No games saved."}, 0x14, 0x14, 0xB0);
        return -1;
    }
    std::vector<std::string> lines = {"Load Game"};
    for (const auto& s : saves) {
        lines.push_back(s.description);
    }
    const int choice = textBox(ui, lines, 0x14, 0x14, 0xB0);
    if (choice < 0 || choice >= static_cast<int>(saves.size())) {
        return -1;
    }
    return saves[static_cast<std::size_t>(choice)].number;
}

GameOverChoice gameOverMenu(Ui& ui, int& load) {
    const std::vector<std::string> lines = {"Game Over", "#GENE\\GENER006#Load a previously saved game.",
                                            "#GENE\\GENER020#Restart the game.", "#GENE\\GENER018#Quit the game."};
    while (!ui.session.platform.quit) {
        switch (textBox(ui, lines, 0x14, 0x14, 0xB0)) {
        case 0:
            load = loadGameMenu(ui);
            if (load >= 0) {
                return GameOverChoice::Load;
            }
            break;
        case 1:
            return GameOverChoice::Restart;
        case 2:
            return GameOverChoice::Quit;
        default:
            break;
        }
    }
    return GameOverChoice::Quit;
}

} // namespace helmsman::away
