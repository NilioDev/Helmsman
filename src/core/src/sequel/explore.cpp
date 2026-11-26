// SPDX-License-Identifier: GPL-3.0-or-later
//
// For the tools: an away mission of the second game played by itself. From
// where the mission starts it tries every order the rooms answer (looking,
// talking, using, taking, walking to a place, going out), keeps each new
// state of the mission it comes to (the room, the scripts' values, what the
// crew carries) as a saved game, and goes on from those, until the mission
// is over or it has tried enough. Started by the tools with
// "explore EPISODE ROOM SPAWN MAX" (see Game::devStart()).
#include "game_impl.hpp"

#include <sstream>

namespace helmsman::sequel::detail {

namespace {

namespace room {
constexpr std::uint16_t kExits = 0x16;
constexpr std::uint16_t kAreas = 0x1A;
} // namespace room

constexpr std::uint16_t kItems = 0x0F5A;     // 10 bytes an item: have, name
constexpr std::uint16_t kHandlers = 0x526A;  // {kind, table, priority} x 7
constexpr std::uint16_t kHandlerCount = 0x5294;
constexpr std::uint16_t kTeam = 0x45;
constexpr std::uint8_t kBeamed = 0x01;
constexpr int kCrewSlots = 9;
constexpr std::uint8_t kExitEvent = 0x06;

// What the state of a mission is made of, for telling states apart: the
// room, the scripts' values past their timers and the event bytes, and
// what is carried.
struct Block {
    std::uint16_t at;
    std::uint16_t size;
};
constexpr Block kStateBlocks[] = {{0xC220, 0x10A}, {0x11DA, 0x280}};

// The orders a player gives in a room: walk to (1), use (2), get (3),
// look (4), talk (5); and two of the explorer's own.
constexpr int kGoOut = 0x100;  // through the way out `a`
constexpr int kStepIn = 0x101; // walk into the touch area `a`
constexpr int kWait = 0x102;   // let time pass
constexpr int kPick = 0x103;   // in a replayed path: the answer the next choice gets

constexpr int kSettleFrames = 12;
constexpr int kStuckFrames = 6000;
constexpr int kMostFailures = 8;
constexpr int kMostAnswers = 12; // an order that met a choice is tried again with other answers

std::string actionText(int type, int a, int b, int variant) {
    char s[48];
    switch (type) {
    case 1:
        std::snprintf(s, sizeof s, "walk:%02x", a);
        break;
    case 2:
        std::snprintf(s, sizeof s, "use:%02x,%02x", a, b);
        break;
    case 3:
        std::snprintf(s, sizeof s, "get:%02x", a);
        break;
    case 4:
        std::snprintf(s, sizeof s, "look:%02x", a);
        break;
    case 5:
        std::snprintf(s, sizeof s, "talk:%02x", a);
        break;
    case kGoOut:
        std::snprintf(s, sizeof s, "out:%d", a);
        break;
    case kStepIn:
        std::snprintf(s, sizeof s, "area:%d", a);
        break;
    case kPick:
        std::snprintf(s, sizeof s, "pick:%d", a);
        break;
    default:
        std::snprintf(s, sizeof s, "wait");
        break;
    }
    std::string text = s;
    if (variant > 0) {
        text += "/" + std::to_string(variant);
    }
    return text;
}

} // namespace

struct Explorer {
    struct Action {
        int type = 0;
        int a = 0;
        int b = 0;
        int variant = 0;
        bool operator<(const Action& o) const {
            return std::tie(type, a, b, variant) < std::tie(o.type, o.a, o.b, o.variant);
        }
    };
    struct Node {
        std::vector<std::uint8_t> payload;
        std::vector<std::uint8_t> state;
        std::vector<Action> actions;
        std::size_t next = 0;
        int failed = 0; // orders that lost the game or got held here
        int progress = 0; // how far into the mission it looks
        int score = 0;    // the points earned
        std::string path;
        int room = 0;
    };
    int most = 3000;
    std::vector<Node> nodes;
    std::map<std::uint64_t, int> seen;
    int expect = -1;   // the node just loaded
    int from = -1;     // the node the order under way was given in
    std::string doing; // and the order
    int settle = 0;
    int stuck = 0;
    int frames = 0;        // since the order under way was given
    int lastFunction = -1; // the room function the scripts called last
    std::mt19937 rng{1};
    int tried = 0;
    std::deque<Action> replay; // orders to give first, as printed in a path
    Action current;            // the order under way
    int choices = 0;           // the choices it met
    int firstCount = 0;        // the answers of the first
};

namespace {

// An order as actionText() writes it.
std::optional<Explorer::Action> readAction(const std::string& w) {
    Explorer::Action a;
    const auto slash = w.find('/');
    const std::string order = w.substr(0, slash);
    if (slash != std::string::npos) {
        a.variant = std::atoi(w.c_str() + slash + 1);
    }
    const auto colon = order.find(':');
    const std::string kind = order.substr(0, colon);
    const std::string rest = colon == std::string::npos ? "" : order.substr(colon + 1);
    const auto hex = [](const std::string& t) { return static_cast<int>(std::strtol(t.c_str(), nullptr, 16)); };
    if (kind == "walk" || kind == "get" || kind == "look") {
        a.type = kind == "walk" ? 1 : kind == "get" ? 3 : 4;
        a.a = hex(rest);
    } else if (kind == "use") {
        a.type = 2;
        const auto comma = rest.find(',');
        a.a = hex(rest.substr(0, comma));
        a.b = comma == std::string::npos ? 0 : hex(rest.substr(comma + 1));
    } else if (kind == "talk") {
        a.type = 5;
        a.a = hex(rest);
    } else if (kind == "out" || kind == "area") {
        a.type = kind == "out" ? kGoOut : kStepIn;
        a.a = std::atoi(rest.c_str());
    } else if (kind == "wait") {
        a.type = kWait;
    } else if (kind == "pick") {
        a.type = kPick;
        a.a = std::atoi(rest.c_str());
    } else {
        return std::nullopt;
    }
    return a;
}

int bestNode(const Explorer& x) {
    int best = -1;
    for (std::size_t i = 0; i < x.nodes.size(); ++i) {
        const auto& n = x.nodes[i];
        if (n.next < n.actions.size() && (best < 0 || n.progress >= x.nodes[static_cast<std::size_t>(best)].progress)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace

void Game::startExplorer(int most, const std::string& path) {
    explore_ = std::make_shared<Explorer>();
    explore_->most = most;
    // Nobody listens either.
    setMusicOn(false);
    soundOn_ = false;
    std::istringstream in(path);
    for (std::string w; in >> w;) {
        if (const auto a = readAction(w)) {
            explore_->replay.push_back(*a);
        }
    }
}

// The explorer's answer to a choice of `count`: the one a replayed path
// gives, or one at random.
int Game::exploreChoice(int count) {
    Explorer& x = *explore_;
    if (!x.replay.empty() && x.replay.front().type == kPick) {
        const int a = x.replay.front().a;
        x.replay.pop_front();
        return std::clamp(a, 0, std::max(0, count - 1));
    }
    if (count <= 1) {
        return 0;
    }
    // The first choice of an order goes through its answers one try after
    // the other; later ones are taken at random.
    if (x.choices++ == 0) {
        x.firstCount = count;
        return x.current.variant % count;
    }
    return static_cast<int>(x.rng() % static_cast<unsigned>(count));
}

// When the order under way met a choice, it is tried again later with other
// answers. True when it will be.
bool Game::exploreOtherAnswers() {
    Explorer& x = *explore_;
    const bool again = x.from >= 0 && x.choices > 0 &&
                       x.current.variant + 1 < std::max(std::min(x.firstCount, kMostAnswers), x.choices > 1 ? 4 : 0);
    if (again) {
        Explorer::Action a = x.current;
        ++a.variant;
        auto& n = x.nodes[static_cast<std::size_t>(x.from)];
        n.actions.insert(n.actions.begin() + static_cast<std::ptrdiff_t>(std::min(n.next, n.actions.size())), a);
    }
    x.choices = 0;
    return again;
}

void Game::exploreJump(const char* why) {
    Explorer& x = *explore_;
    if (!why) {
        why = "";
    }
    if (*why && x.from >= 0) {
        Explorer::Node& n = x.nodes[static_cast<std::size_t>(x.from)];
        std::printf("%s after %s %s\n", why, n.path.c_str(), x.doing.c_str());
        std::fflush(stdout);
        // A state where order after order goes wrong is lost already (a
        // timer runs out, say): no more of it.
        if (!exploreOtherAnswers() && ++n.failed >= kMostFailures) {
            n.next = n.actions.size();
        }
    }
    // The state that looks furthest into the mission, with orders left to
    // try (the newest of those).
    if (const int i = bestNode(x); i >= 0) {
        x.from = -1;
        x.settle = 0;
        x.stuck = 0;
        x.frames = 0;
        x.expect = i;
        throw RestoreGame{x.nodes[static_cast<std::size_t>(i)].payload, -1};
    }
    std::printf("explored: %zu states, %d orders, nothing left to try\n", x.nodes.size(), x.tried);
    p_.quit = true;
    throw LeaveMission{0};
}

void Game::exploreEnd(int how) {
    Explorer& x = *explore_;
    const std::string path = x.from >= 0 ? x.nodes[static_cast<std::size_t>(x.from)].path + " " + x.doing : "";
    if (how == 1) {
        std::printf("mission over (%d %d) after %zu states, %d orders: %s\n", m_.s16(0xC222), m_.s16(0xC224),
                    x.nodes.size(), x.tried, path.c_str());
        std::fflush(stdout);
        p_.quit = true;
        return;
    }
    // Beamed up before the end: not the way through.
    exploreJump("beamed up");
}

// Each frame shown: a script that keeps the game waiting for too long (one
// that waits for the player inside its own loop) is given up.
void Game::exploreFrame() {
    Explorer& x = *explore_;
    if (++x.frames > kStuckFrames * 3 && x.from >= 0) {
        if (p_.devTrace) {
            std::printf("  held in a script, room function %02x last\n", x.lastFunction);
        }
        char why[64];
        std::snprintf(why, sizeof why, "held (room function %02x)", x.lastFunction);
        exploreJump(why);
    }
}

void Game::exploreCalled(int function) { explore_->lastFunction = function; }

void Game::exploreIdle() {
    Explorer& x = *explore_;
    const Body& kirk = bodies_[0];
    const bool idle = !busy_ && queue_.empty() && !kirk.walking;
    if (!idle) {
        x.settle = 0;
        if (++x.stuck > kStuckFrames) {
            if (p_.devTrace) {
                std::printf("  stuck: busy %d, %zu events, walking %d at %d,%d\n", busy_ ? 1 : 0, queue_.size(),
                            kirk.walking ? 1 : 0, kirk.x, kirk.y);
            }
            exploreJump("stuck");
        }
        return;
    }
    if (++x.settle < kSettleFrames) {
        return;
    }
    x.stuck = 0;
    exploreOtherAnswers();
    // Where are we?
    std::uint64_t key = 1469598103934665603ULL ^ static_cast<std::uint64_t>(roomNumber_ & 0xFF);
    std::vector<std::uint8_t> state;
    for (const auto& b : kStateBlocks) {
        for (std::uint16_t i = 0; i < b.size; ++i) {
            const std::uint8_t v = m_.u8(static_cast<std::uint16_t>(b.at + i));
            state.push_back(v);
            key = (key ^ v) * 1099511628211ULL;
        }
    }
    int node = -1;
    if (x.expect >= 0) {
        node = x.expect;
        x.expect = -1;
        x.seen.emplace(key, node);
    } else if (const auto found = x.seen.find(key); found != x.seen.end()) {
        node = found->second;
    } else {
        Explorer::Node n;
        n.room = roomNumber_;
        n.path = x.from >= 0 ? x.nodes[static_cast<std::size_t>(x.from)].path + " " + x.doing : "start";
        n.payload = savePayload();
        n.state = state;
        // Progress: the values set, and the points earned above all.
        for (const auto v : state) {
            n.progress += v != 0 ? 1 : 0;
        }
        n.score = m_.s16(0xC224);
        n.progress += 64 * n.score;
        if (p_.devTrace && x.from >= 0) {
            // What tells it from the state the order was given in.
            const auto& before = x.nodes[static_cast<std::size_t>(x.from)].state;
            std::size_t k = 0;
            for (const auto& b : kStateBlocks) {
                for (std::uint16_t i = 0; i < b.size; ++i, ++k) {
                    if (k < before.size() && before[k] != state[k]) {
                        std::printf("  DS:%04X %02x -> %02x\n", b.at + i, before[k], state[k]);
                    }
                }
            }
        }
        // The orders the rooms answer now.
        // The orders the rooms answer now: the room's own first, then the
        // mission's, then those any thing answers; then the places to walk
        // to and the ways out.
        std::map<Explorer::Action, int> orders; // and how soon to try them
        const auto add = [&orders](Explorer::Action a, int rank) {
            const auto [it, fresh] = orders.emplace(a, rank);
            if (!fresh) {
                it->second = std::min(it->second, rank);
            }
        };
        std::vector<int> subjects;
        for (int i = 0; i < kCrewSlots; ++i) {
            if ((rec8(i, kTeam) & kBeamed) != 0) {
                subjects.push_back(i);
            }
        }
        for (int item = 0x40; item < 0x80; ++item) {
            if (m_.u8(static_cast<std::uint16_t>(kItems + 10 * item)) != 0) {
                subjects.push_back(item);
            }
        }
        const int count = std::clamp<int>(m_.s16(kHandlerCount), 0, 7);
        for (int h = 0; h < count; ++h) {
            const auto entry = static_cast<std::uint16_t>(kHandlers + 6 * h);
            const int kind = m_.s16(entry);
            const std::uint16_t seg = handlerSeg(kind);
            const std::uint16_t table = m_.u16(static_cast<std::uint16_t>(entry + 2));
            const int rank = std::clamp(kind, 0, 2) * 2;
            for (int list = 0; list < 4; ++list) {
                std::uint16_t a = m_.word(seg, static_cast<std::uint16_t>(table + 4 * list));
                const std::uint16_t end = m_.word(seg, static_cast<std::uint16_t>(table + 4 * list + 2));
                for (int guard = 0; a < end && guard < 4096; ++guard) {
                    std::uint8_t e[4];
                    for (int k = 0; k < 4; ++k) {
                        e[k] = m_.cpu.read8(seg, static_cast<std::uint16_t>(a + k));
                    }
                    a = m_.word(seg, static_cast<std::uint16_t>(a + 4));
                    if (e[0] < 1 || e[0] > 5 || e[3] != 0) {
                        continue;
                    }
                    // Things to carry count only once carried.
                    const auto held = [this](int v) {
                        return v == 0xFF || v < 0x40 || v >= 0x80 ||
                               m_.u8(static_cast<std::uint16_t>(kItems + 10 * v)) != 0;
                    };
                    if (!held(e[1]) || (e[0] == 2 && !held(e[2]))) {
                        continue;
                    }
                    if (e[0] == 2) {
                        if (e[2] == 0xFF) {
                            continue;
                        }
                        if (e[1] != 0xFF) {
                            add({2, e[1], e[2], 0}, rank);
                        } else if (kind < 2) {
                            // Anything used on it: what the team has.
                            for (int sub : subjects) {
                                add({2, sub, e[2], 0}, rank + 1);
                            }
                        }
                    } else if (e[1] != 0xFF) {
                        add({e[0], e[1], 0, 0}, rank);
                    }
                }
            }
        }
        std::vector<std::pair<int, Explorer::Action>> ranked;
        for (const auto& [a, r] : orders) {
            ranked.emplace_back(r, a);
        }
        for (const auto& p : polygons(room::kAreas, room::kAreas + 2)) {
            ranked.emplace_back(6, Explorer::Action{kStepIn, p.id, 0, 0});
        }
        for (const auto& p : polygons(room::kExits, room::kExits + 2)) {
            ranked.emplace_back(6, Explorer::Action{kGoOut, p.id, 0, 0});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto& l, const auto& r) { return l.first < r.first; });
        for (const auto& [r, a] : ranked) {
            n.actions.push_back(a);
        }
        n.actions.push_back({kWait, 0, 0, 0});
        node = static_cast<int>(x.nodes.size());
        x.seen.emplace(key, node);
        if (p_.devTrace) {
            std::printf("state %d room %d, %zu orders: %s\n", node, n.room, n.actions.size(), n.path.c_str());
        }
        x.nodes.push_back(std::move(n));
        if (static_cast<int>(x.nodes.size()) % 100 == 0) {
            std::printf("%zu states, %d orders\n", x.nodes.size(), x.tried);
            std::fflush(stdout);
        }
        if (static_cast<int>(x.nodes.size()) >= x.most) {
            std::printf("stopped at %zu states, %d orders\n", x.nodes.size(), x.tried);
            std::map<int, int> rooms;
            const Explorer::Node* furthest = &x.nodes.front();
            for (const auto& o : x.nodes) {
                ++rooms[o.room];
                if (o.progress > furthest->progress) {
                    furthest = &o;
                }
            }
            for (const auto& [r, k] : rooms) {
                std::printf("  room %d: %d states\n", r, k);
            }
            std::printf("furthest (%d, %d points) in room %d: %s\n", furthest->progress, furthest->score, furthest->room,
                        furthest->path.c_str());
            p_.quit = true;
            throw LeaveMission{0};
        }
    }
    Explorer::Node& n = x.nodes[static_cast<std::size_t>(node)];
    if (x.replay.empty()) {
        const int best = bestNode(x);
        if (n.next >= n.actions.size() || (best >= 0 && x.nodes[static_cast<std::size_t>(best)].progress > n.progress)) {
            x.from = -1;
            exploreJump(nullptr);
        }
    }
    while (!x.replay.empty() && x.replay.front().type == kPick) {
        x.replay.pop_front();
    }
    Explorer::Action act;
    if (!x.replay.empty()) {
        act = x.replay.front();
        x.replay.pop_front();
    } else {
        act = n.actions[n.next++];
    }
    ++x.tried;
    x.from = node;
    x.current = act;
    x.choices = 0;
    x.doing = actionText(act.type, act.a, act.b, act.variant);
    if (p_.devTrace) {
        std::printf("> %s (room %d)\n", x.doing.c_str(), roomNumber_);
    }
    x.settle = 0;
    x.frames = 0;
    x.rng.seed(static_cast<unsigned>(node * 7919 + act.type * 131 + act.a * 17 + act.b * 3 + act.variant * 1000003));
    switch (act.type) {
    case kGoOut:
        post(kExitEvent, static_cast<std::uint8_t>(act.a));
        break;
    case kStepIn: {
        for (const auto& p : polygons(room::kAreas, room::kAreas + 2)) {
            if (p.id != act.a || p.points.empty()) {
                continue;
            }
            long sx = 0;
            long sy = 0;
            for (const auto& [px, py] : p.points) {
                sx += px;
                sy += py;
            }
            int cx = static_cast<int>(sx / static_cast<long>(p.points.size()));
            int cy = static_cast<int>(sy / static_cast<long>(p.points.size()));
            if (!p.contains(cx, cy)) {
                cx = p.points.front().first;
                cy = p.points.front().second;
            }
            walk(0, -1, -1, cx, cy, 0);
        }
        break;
    }
    case kWait:
        waitAwayTicks(200);
        break;
    default:
        m_.set16(0xC212, static_cast<std::uint16_t>(kirk.x));
        m_.set16(0xC214, static_cast<std::uint16_t>(kirk.y));
        post(static_cast<std::uint8_t>(act.type), static_cast<std::uint8_t>(act.a), static_cast<std::uint8_t>(act.b));
        break;
    }
}

} // namespace helmsman::sequel::detail
