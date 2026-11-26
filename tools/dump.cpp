// SPDX-License-Identifier: GPL-3.0-or-later
//
// helmsman-dump: lists the files in your own game's archives, unpacks one,
// or writes its pictures as PNG files, to check the readers.
//
//   helmsman-dump IMAGE-OR-FOLDER list [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER file NAME OUT [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER picture NAME PALETTE OUT.png [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER disc
//   helmsman-dump IMAGE-OR-FOLDER room MISSION ROOM SPAWN TICKS OUT.png
//   helmsman-dump IMAGE-OR-FOLDER music FILE SONG SECONDS OUT.wav
//   helmsman-dump IMAGE-OR-FOLDER play MISSION ROOM SPAWN SCRIPT OUT-FOLDER
//
// A play script has one step per line, "FRAME WHAT ...", frames counted
// from the start: "move X Y", "left", "right" (press and release), "key
// enter|escape|space|up|down|left|right|CHAR", "shot NAME" (writes
// NAME.png with the pointer) and "end".
#include "helmsman/audio/adlib.hpp"
#include "helmsman/away/mission.hpp"
#include "helmsman/bridge/bridge.hpp"
#include "helmsman/engine/play.hpp"
#include "helmsman/space/space.hpp"
#include "helmsman/space/planet.hpp"
#include "helmsman/data/program.hpp"
#include "helmsman/data/game_source.hpp"
#include "helmsman/engine/fiber.hpp"
#include "helmsman/data/archive.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"
#include "helmsman/gfx/png.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include <string>

using namespace helmsman;

namespace {

void writeShot(const std::string& path, const Platform& p) {
    Surface shot = p.screen;
    if (p.pointerShown) {
        for (int y = 0; y < p.pointer.height(); ++y) {
            for (int x = 0; x < p.pointer.width(); ++x) {
                const int sx = p.mouseX - p.pointerHotX + x;
                const int sy = p.mouseY - p.pointerHotY + y;
                const auto c = p.pointer.at(x, y);
                if (c != 0) {
                    shot.set(sx, sy, c);
                }
            }
        }
    }
    writePng(path, shot, p.palette);
}

struct Step {
    std::string what;
    std::vector<std::string> args;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: helmsman-dump IMAGE-OR-FOLDER list|file|picture ...\n");
        return 2;
    }
    try {
        auto source = GameSource::open(argv[1]);
        if (!source) {
            std::fprintf(stderr, "no game found there\n");
            return 1;
        }
        const std::string what = argv[2];
        auto archiveArg = [&](int index) { return argc > index ? std::string(argv[index]) : std::string("DATA"); };
        if (what == "list") {
            const auto archive = Archive::open(*source, archiveArg(3));
            for (const auto& name : archive.names()) {
                std::printf("%s\n", name.c_str());
            }
        } else if (what == "file" && argc >= 5) {
            const auto archive = Archive::open(*source, archiveArg(5));
            const auto data = archive.need(argv[3]);
            std::ofstream out(argv[4], std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        } else if (what == "picture" && argc >= 6) {
            const auto archive = Archive::open(*source, archiveArg(6));
            const auto bitmap = Bitmap::fromBytes(archive.need(argv[3]));
            const auto palette = Palette::fromBytes(archive.need(argv[4]));
            writePng(argv[5], bitmap.pixels, palette);
            std::printf("%dx%d, handle %d,%d\n", bitmap.width(), bitmap.height(), bitmap.handleX, bitmap.handleY);
        } else if (what == "planets" && argc >= 4) {
            // Every system's planet seen from a few sides, in one picture.
            const auto archive = Archive::open(*source, "DATA");
            const auto program = source->read(programName(source->game()));
            const auto image = programImage(*program);
            const auto look = space::PlanetLook::load(archive, image);
            if (!look) {
                std::fprintf(stderr, "no planet data\n");
                return 1;
            }
            const int seeds[][2] = {{0x1c5, 6}, {0x125, 0xa}, {0x205, 0xb}, {0x13e, 0xc}, {0x39c, 0x12}, {0x155, 7},
                                    {0x358, 8}, {0x87, 0},  {0x188, 0xe}, {0x283, 0xd}, {0x340, 0xc}, {0x155, 0xb},
                                    {0x347, 0xa}, {0x3a4, 7}, {0x285, 0xe}, {0xea, 0xf}, {0x155, 0x10}, {0x373, 0x11},
                                    {0x2f0, 9}, {0xea, 9}};
            Surface sheet(320, 200);
            for (int i = 0; i < 20; ++i) {
                const auto surface = space::planetSurface(*look, seeds[i][0], seeds[i][1]);
                const auto pic = space::planetPicture(*look, surface, 4, i * 40, (i % 5) * 40 - 80);
                pic.draw(sheet, 32 + (i % 5) * 64, 32 + (i / 5) * 64 - 20);
            }
            writePng(argv[3], sheet, Palette::fromBytes(archive.need("BRIDGE.PAL")));
        } else if (what == "raw" && argc >= 5) {
            auto data = source->read(argv[3]);
            if (!data) {
                std::fprintf(stderr, "no such file\n");
                return 1;
            }
            std::ofstream out(argv[4], std::ios::binary);
            out.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
        } else if (what == "room" && argc >= 8) {
            // Plays a room of a planet mission without a player for a while.
            Platform platform;
            int frames = 0;
            platform.onWait = [&frames](Platform& p) {
                p.millis += 16;
                if (++frames % 40 == 0) {
                    p.keys.push_back(KeyPress{scancode::kEnter, 0});
                }
            };
            away::Session session(platform, *source);
            away::Mission mission(session, argv[3]);
            mission.loadRoom(std::stoi(argv[4]));
            mission.spawnCrew(std::stoi(argv[5]));
            const int ticks = std::stoi(argv[6]);
            for (int i = 0; i < ticks; ++i) {
                mission.advance();
            }
            writePng(argv[7], platform.screen, platform.palette);
            std::printf("%s", mission.describe().c_str());
        } else if (what == "events" && argc >= 5) {
            // Every event a room answers, each in a fresh copy of the room,
            // run until things settle: for finding room code that fails.
            int frames = 0;
            // HELMSMAN_SHOT=FRAME:FILE saves the screen at that frame.
            int shotAt = 0;
            std::string shotPath;
            if (const char* shot = std::getenv("HELMSMAN_SHOT")) {
                const std::string v = shot;
                const auto colon = v.find(':');
                if (colon != std::string::npos) {
                    shotAt = std::atoi(v.substr(0, colon).c_str());
                    shotPath = v.substr(colon + 1);
                }
            }
            Platform platform;
            platform.onWait = [&frames, shotAt, &shotPath](Platform& p) {
                p.millis += 16;
                ++frames;
                for (auto& q : p.soundQueued) {
                    q = 0.0;
                }
                p.sound.clear();
                // Press Enter now and then, and click all over the screen,
                // so that boxes and maps that wait for the player go on.
                if (frames % 25 == 0) {
                    p.keys.push_back(KeyPress{scancode::kEnter, 0});
                    const auto k = static_cast<unsigned>(frames / 25) * 2654435761u;
                    p.mouseX = static_cast<int>((k >> 8) % 320);
                    p.mouseY = static_cast<int>((k >> 20) % 200);
                    p.mouseButtons = 1;
                } else if (frames % 25 == 1) {
                    p.mouseButtons = 0;
                }
                if (shotAt > 0 && frames == shotAt) {
                    writePng(shotPath, p.screen, p.palette);
                }
                if (frames > 20000) {
                    throw StopRequested{};
                }
            };
            const int room = std::stoi(argv[4]);
            std::vector<away::Event> list;
            {
                away::Session session(platform, *source);
                away::Mission m(session, argv[3]);
                m.loadRoom(room);
                list = m.roomEvents();
            }
            int failed = 0;
            for (const auto& e : list) {
                if (argc >= 6) {
                    char id[16];
                    std::snprintf(id, sizeof id, "%02x%02x%02x%02x", e.action, e.b1, e.b2, e.b3);
                    if (std::string(argv[5]) != id) {
                        continue;
                    }
                }
                frames = 0;
                std::string outcome;
                try {
                    away::Session session(platform, *source);
                    session.trace = std::getenv("HELMSMAN_TRACE") != nullptr;
                    away::Mission m(session, argv[3]);
                    m.loadRoom(room);
                    m.spawnCrew(0);
                    for (int i = 0; i < 60; ++i) {
                        m.advance();
                    }
                    m.inject(e);
                    for (int i = 0; i < 400 && !m.ended() && m.roomChangingTo() < 0; ++i) {
                        m.advance();
                    }
                    outcome = m.ended() ? "ends the mission" : m.roomChangingTo() >= 0 ? "to room " + std::to_string(m.roomChangingTo()) : "ok";
                } catch (const StopRequested&) {
                    outcome = "STUCK";
                    ++failed;
                } catch (const std::exception& ex) {
                    outcome = std::string("FAILED: ") + ex.what();
                    ++failed;
                }
                std::printf("%s%d %02x %02x %02x %02x: %s\n", argv[3], room, e.action, e.b1, e.b2, e.b3, outcome.c_str());
            }
            std::printf("%zu events, %d failed\n", list.size(), failed);
        } else if (what == "play" && argc >= 8) {
            // Plays a mission with input from a script.
            std::multimap<int, Step> steps;
            {
                std::ifstream in(argv[6]);
                std::string line;
                while (std::getline(in, line)) {
                    std::istringstream words(line);
                    int frame = 0;
                    Step step;
                    if (!(words >> frame >> step.what)) {
                        continue;
                    }
                    for (std::string w; words >> w;) {
                        step.args.push_back(w);
                    }
                    steps.emplace(frame, step);
                }
            }
            const std::string outFolder = argv[7];
            Platform platform;
            platform.saveFolder = outFolder + "/saves";
            int frame = 0;
            int release = -1;
            platform.onWait = [&](Platform& p) {
                p.millis += 16;
                ++frame;
                // The sound plays away as on a real computer.
                for (auto& q : p.soundQueued) {
                    q = q > 0.016 ? q - 0.016 : 0.0;
                }
                p.sound.clear();
                if (frame == release) {
                    p.mouseButtons = 0;
                }
                if (p.warpPointer) {
                    p.mouseX = p.warpX;
                    p.mouseY = p.warpY;
                    p.warpPointer = false;
                }
                auto [from, to] = steps.equal_range(frame);
                for (auto it = from; it != to; ++it) {
                    const Step& st = it->second;
                    if (st.what == "move" && st.args.size() >= 2) {
                        p.mouseX = std::stoi(st.args[0]);
                        p.mouseY = std::stoi(st.args[1]);
                    } else if (st.what == "left" || st.what == "right") {
                        p.mouseButtons = st.what == "left" ? 1 : 2;
                        release = frame + 8;
                    } else if (st.what == "key" && !st.args.empty()) {
                        static const std::map<std::string, std::uint8_t> named = {
                            {"enter", scancode::kEnter}, {"escape", scancode::kEscape}, {"space", scancode::kSpace},
                            {"up", scancode::kUp}, {"down", scancode::kDown}, {"left", scancode::kLeft},
                            {"right", scancode::kRight}, {"tab", scancode::kTab}};
                        const auto found = named.find(st.args[0]);
                        if (found != named.end()) {
                            p.keys.push_back(KeyPress{found->second, 0});
                        } else {
                            // The PC keyboard's numbers for letters and digits.
                            static const std::string rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm,."};
                            static const std::uint8_t starts[] = {0x02, 0x10, 0x1E, 0x2C};
                            const char c = st.args[0][0];
                            std::uint8_t code = c == '`' ? 0x29 : 0;
                            for (int r = 0; r < 4; ++r) {
                                const auto at = rows[r].find(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                                if (at != std::string::npos) {
                                    code = static_cast<std::uint8_t>(starts[r] + at);
                                }
                            }
                            p.keys.push_back(KeyPress{code, c});
                        }
                    } else if (st.what == "shot" && !st.args.empty()) {
                        writeShot(outFolder + "/" + st.args[0] + ".png", p);
                    } else if (st.what == "end") {
                        throw StopRequested{};
                    }
                }
            };
            away::Session session(platform, *source);
            session.trace = std::getenv("HELMSMAN_TRACE") != nullptr;
            try {
                if (std::string(argv[3]) == "GAME") {
                    playGame(platform, *source);
                    std::printf("game ended\n");
                } else if (std::string(argv[3]) == "BRIDGE") {
                    static bridge::Voyage voyage;
                    if (const char* m = std::getenv("HELMSMAN_MISSION")) {
                        voyage.mission = std::atoi(m);
                    }
                    if (std::getenv("HELMSMAN_ENDING") != nullptr) {
                        // The last battle just won: the verdicts and the closing titles.
                        voyage.mission = 7;
                        voyage.stage = 1;
                        voyage.textFile = "veng";
                    }
                    bridge::Bridge b(session, voyage);
                    if (const char* ships = std::getenv("HELMSMAN_SHIPS")) {
                        // For trying space out: ships to meet, "KLINGON,ORION".
                        std::string list = ships;
                        for (std::size_t at = 0; at <= list.size();) {
                            const auto comma = list.find(',', at);
                            const auto name = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                            if (!name.empty()) {
                                const int i = b.space().addShip(name);
                                if (i >= 0) {
                                    b.space().object(i).pos = space::Vec{(i - 2) * 400, 100, 1500};
                                }
                            }
                            if (comma == std::string::npos) break;
                            at = comma + 1;
                        }
                    }
                    if (const char* still = std::getenv("HELMSMAN_STILL")) {
                        // For trying stills out: 4 ark7 .. 7 republic.
                        b.approach(std::atoi(still));
                    }
                    const auto result = b.run();
                    std::printf("bridge ended: %d\n", static_cast<int>(result.leave));
                } else {
                    const auto result = away::playAway(session, argv[3], std::stoi(argv[4]), std::stoi(argv[5]));
                    std::printf("mission ended: score %d, %d %d%s\n", result.score, result.a, result.b, result.quit ? ", quit" : "");
                }
            } catch (const StopRequested&) {
                std::printf("script ended at frame %d\n", frame);
            }
        } else if (what == "music" && argc >= 7) {
            // Plays a song of a music file into a WAV file.
            const auto archive = Archive::open(*source, "DATA");
            audio::AdLibMusic music(44100);
            music.setInstruments(archive.need("GTL.AD"));
            music.setSongs(audio::readXmidi(archive.need(argv[3])));
            music.play(std::stoi(argv[4]));
            const auto frames = static_cast<std::size_t>(std::stod(argv[5]) * 44100);
            std::vector<std::int16_t> pcm(frames * 2);
            music.render(pcm.data(), frames);
            std::ofstream out(argv[6], std::ios::binary);
            auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
            const auto bytes = static_cast<std::uint32_t>(pcm.size() * 2);
            out.write("RIFF", 4);
            u32(36 + bytes);
            out.write("WAVEfmt ", 8);
            u32(16);
            u16(1);
            u16(2);
            u32(44100);
            u32(44100 * 4);
            u16(4);
            u16(16);
            out.write("data", 4);
            u32(bytes);
            out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(bytes));
            std::printf("%d songs, song %d %s\n", music.songCount(), std::stoi(argv[4]), music.playing() ? "still playing" : "ended");
        } else if (what == "space" && argc >= 6) {
            // Flies the player's ship among others for a while:
            //   space SHIP,SHIP,... TICKS OUT.png [ZOOM]
            const auto archive = Archive::open(*source, "DATA");
            space::Space sp(archive);
            sp.setView(ClipRect{0, 0, 320, 200}, argc > 6 ? std::stoi(argv[6]) : 4);
            sp.setPlayer("ENTERPRI");
            sp.player().weaponsArmed = true;
            std::string list = argv[3];
            for (std::size_t at = 0; at < list.size();) {
                const auto comma = list.find(',', at);
                const auto name = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                if (name.rfind("still:", 0) == 0) {
                    sp.addStill(name.substr(6), space::Vec{0, 0, 1500});
                } else if (!name.empty()) {
                    const int i = sp.addShip(name);
                    if (i >= 0) {
                        sp.object(i).pos = sp.player().pos + space::Vec{(i - 2) * 300, 0, 1200};
                    }
                }
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            const int ticks = std::stoi(argv[4]);
            Surface screen;
            for (int t = 0; t < ticks; ++t) {
                sp.tick();
            }
            sp.draw(screen);
            writePng(argv[5], screen, Palette::fromBytes(archive.need("BRIDGE.PAL")));
            for (int i = 0; i < space::Space::kObjects; ++i) {
                const auto& o = sp.object(i);
                if (o.kind == space::Kind::None) continue;
                std::printf("%d kind %d type %d pos %d %d %d hull %d%s\n", i, static_cast<int>(o.kind), o.type, o.pos.x, o.pos.y,
                            o.pos.z, o.cls.parts[0].now, o.destroyed ? " destroyed" : "");
            }
        } else if (what == "check") {
            // Unpacks every file of an archive.
            const auto archive = Archive::open(*source, archiveArg(3));
            int count = 0;
            for (const auto& name : archive.names()) {
                try {
                    archive.need(name);
                    ++count;
                } catch (const FormatError& e) {
                    std::printf("%s: %s\n", name.c_str(), e.what());
                }
            }
            std::printf("%d files unpacked\n", count);
        } else {
            std::fprintf(stderr, "unknown command\n");
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
