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
            int frame = 0;
            int release = -1;
            platform.onWait = [&](Platform& p) {
                p.millis += 16;
                ++frame;
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
                            p.keys.push_back(KeyPress{0, st.args[0][0]});
                        }
                    } else if (st.what == "shot" && !st.args.empty()) {
                        writeShot(outFolder + "/" + st.args[0] + ".png", p);
                    } else if (st.what == "end") {
                        throw StopRequested{};
                    }
                }
            };
            away::Session session(platform, *source);
            away::Mission mission(session, argv[3]);
            mission.setTrace(std::getenv("HELMSMAN_TRACE") != nullptr);
            try {
                const auto result = mission.run(std::stoi(argv[4]), std::stoi(argv[5]));
                std::printf("mission ended: score %d, %d %d%s\n", result.score, result.a, result.b, result.quit ? ", quit" : "");
            } catch (const StopRequested&) {
                std::printf("script ended at frame %d\n", frame);
            }
            std::printf("%s", mission.describe().c_str());
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
