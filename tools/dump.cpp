// SPDX-License-Identifier: GPL-3.0-or-later
//
// helmsman-dump: lists the files in your own game's archives, unpacks one,
// or writes its pictures as PNG files, to check the readers.
//
//   helmsman-dump IMAGE-OR-FOLDER list [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER file NAME OUT [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER picture NAME PALETTE OUT.png [ARCHIVE]
//   helmsman-dump IMAGE-OR-FOLDER disc
#include "helmsman/data/archive.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/gfx/bitmap.hpp"
#include "helmsman/gfx/palette.hpp"
#include "helmsman/gfx/png.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace helmsman;

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
