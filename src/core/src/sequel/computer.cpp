// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's library computer (Spock's "Computer" button and a room
// function) and the captain's log (Kirk's record of the episodes done). See
// docs/rules/sequel-computer.md.
#include "game_impl.hpp"

namespace helmsman::sequel::detail {

namespace {

constexpr std::size_t kMostFound = 8;
constexpr std::uint16_t kEpisodeNames = 0x263A; // per episode: its long name's place, its short one's

std::string upperText(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

// The entries whose words hold the topic: each line of the key file is an
// entry's name, then its words, the list ending at a '$'.
std::vector<std::string> findTopic(const std::vector<std::uint8_t>& key, const std::string& topic) {
    std::vector<std::string> found;
    std::string trimmed = topic;
    while (!trimmed.empty() && trimmed.back() == ' ') {
        trimmed.pop_back();
    }
    while (!trimmed.empty() && trimmed.front() == ' ') {
        trimmed.erase(trimmed.begin());
    }
    if (trimmed.size() <= 2) {
        return found;
    }
    const std::string wanted = " " + upperText(trimmed);
    std::string line;
    for (std::size_t i = 0; i <= key.size(); ++i) {
        const char c = i < key.size() ? static_cast<char>(key[i]) : '\n';
        if (c == '$' && line.empty()) {
            break;
        }
        if (c != '\n') {
            if (c != '\r') {
                line.push_back(c);
            }
            continue;
        }
        const auto space = line.find(' ');
        if (space != std::string::npos && space > 0) {
            const std::string words = upperText(line.substr(space));
            if (words.find(wanted) != std::string::npos && found.size() < kMostFound) {
                found.push_back(line.substr(0, std::min<std::size_t>(space, 8)));
            }
        }
        line.clear();
    }
    return found;
}

// An entry: its first line is its spoken tag and title ("#001#Acolytes"),
// the rest its text. Lines run on; an empty line starts a new paragraph.
struct Entry {
    std::string tag;
    std::string title;
    std::string text;
};

Entry readEntry(const std::vector<std::uint8_t>& bytes) {
    Entry e;
    std::vector<std::string> lines(1);
    for (const auto b : bytes) {
        if (b == '$') {
            break; // the end of the entry
        }
        if (b == '\n') {
            lines.emplace_back();
        } else if (b != '\r' && b != 0x1A) {
            lines.back().push_back(static_cast<char>(b));
        }
    }
    std::string first = lines.front();
    if (first.size() > 1 && first[0] == '#') {
        const auto end = first.find('#', 1);
        if (end != std::string::npos) {
            e.tag = first.substr(0, end + 1);
            first.erase(0, end + 1);
        }
    }
    e.title = first;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string l = lines[i];
        while (!l.empty() && l.back() == ' ') {
            l.pop_back();
        }
        if (l.empty()) {
            if (!e.text.empty() && e.text.back() != ' ') {
                e.text += "  ";
            }
            continue;
        }
        if (!e.text.empty() && e.text.back() != ' ') {
            e.text.push_back(' ');
        }
        e.text += l;
    }
    return e;
}

} // namespace

void Game::libraryComputer() {
    std::optional<SoundBank> bank;
    try {
        bank.emplace(assets_.source(), "COMPUTER.VCC");
    } catch (const FormatError&) {
    }
    computerVoices_ = bank ? &*bank : nullptr;
    struct Done {
        const SoundBank*& v;
        ~Done() { v = nullptr; }
    } done{computerVoices_};
    const auto key = assets_.data().load("DB.KEY");
    const std::string computer = m_.text(prog::kComputer, 512);
    while (!p_.quit) {
        const std::string topic = askText(m_.text(prog::kSelectTopic, 512));
        if (topic.find_first_not_of(' ') == std::string::npos) {
            break;
        }
        const auto found = key ? findTopic(*key, topic) : std::vector<std::string>{};
        std::vector<Entry> entries;
        for (const auto& name : found) {
            if (const auto bytes = assets_.data().load(name + ".DB")) {
                entries.push_back(readEntry(*bytes));
            }
        }
        if (entries.empty()) {
            sayAs(computer, m_.text(prog::kNoReferences, 512));
            continue;
        }
        std::size_t pick = 0;
        if (entries.size() > 1) {
            std::vector<std::string> titles;
            for (const auto& e : entries) {
                titles.push_back(e.title);
            }
            speakerName_ = &computer;
            const int chosen = say(-1, titles, 0);
            speakerName_ = nullptr;
            if (chosen < 0 || chosen >= static_cast<int>(entries.size())) {
                continue;
            }
            pick = static_cast<std::size_t>(chosen);
        }
        const Entry& e = entries[pick];
        sayAs(e.title.empty() ? computer : e.title, e.tag + e.text);
    }
}

// The program's line breaks become the spaces our text boxes wrap at.
static std::string flat(std::string s) {
    for (auto& c : s) {
        if (c == '\n') {
            c = ' ';
        }
    }
    return s;
}

// The points the admiral gave for each episode done so far, the latest
// first.
void Game::captainsLog() {
    const int done = std::clamp<int>(m_.s16(ds::kEpisode), 0, 8);
    const std::string log = "Captain's Log";
    if (done == 0) {
        sayAs(log, m_.text(prog::kNoMissions, 512));
        return;
    }
    int total = 0;
    std::string lines;
    for (int i = done - 1; i >= 0; --i) {
        const int points = m_.s16(static_cast<std::uint16_t>(kRatings + i * 2));
        total += points;
        const auto name = m_.text(m_.u16(static_cast<std::uint16_t>(kEpisodeNames + i * 4)), 40);
        if (name.empty()) {
            continue;
        }
        std::string each = m_.text(points == 1 ? prog::kOnePointEach : prog::kPointsEach, 512);
        if (const auto at = each.find("%d"); at != std::string::npos) {
            each.replace(at, 2, std::to_string(points));
        }
        lines += name + flat(each);
    }
    sayAs(log, m_.text(prog::kEarned, 512) + std::to_string(total) +
                   m_.text(total == 1 ? prog::kPoint : prog::kPoints, 512) +
                   flat(m_.text(prog::kForMissions, 512)) + lines);
}

} // namespace helmsman::sequel::detail
