// SPDX-License-Identifier: GPL-3.0-or-later
//
// Disc images of each kind, made here from nothing: a tiny CD file system
// with a few files and one music track, wrapped the way each program that
// makes images keeps it.
#include "test.hpp"

#include "helmsman/data/disc.hpp"
#include "helmsman/data/game_source.hpp"
#include "helmsman/data/reader.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace helmsman;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kCooked = 2048;
constexpr std::size_t kRaw = 2352;
constexpr std::uint32_t kDataSectors = 24;
constexpr std::uint32_t kAudioSectors = 10;

void putLe(Bytes& b, std::size_t at, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}
void putBe(Bytes& b, std::size_t at, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * (n - 1 - i)));
}
// Both byte orders, as the CD file system keeps its numbers.
void putBoth(Bytes& b, std::size_t at, std::uint32_t v) {
    putLe(b, at, v, 4);
    putBe(b, at + 4, v, 4);
}
void append(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

struct File {
    const char* name;
    std::string text;
};
const File kFiles[] = {{"TREKJR.EXE", "program"}, {"DATA.DIR", "directory"}, {"HELLO.TXT", "Hello, captain."}};

// 2048-byte sectors: the volume descriptor at 16, the end of descriptors at
// 17, the top folder at 18, the files from 19 on.
Bytes cookedImage() {
    Bytes iso(kDataSectors * kCooked, 0);
    auto record = [&](std::size_t at, std::uint32_t sector, std::uint32_t size, std::uint8_t flags, const std::string& name) {
        const auto len = static_cast<std::uint8_t>(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
        iso[at] = len;
        putBoth(iso, at + 2, sector);
        putBoth(iso, at + 10, size);
        iso[at + 25] = flags;
        iso[at + 32] = static_cast<std::uint8_t>(name.size());
        std::memcpy(&iso[at + 33], name.data(), name.size());
        return at + len;
    };
    const std::size_t pvd = 16 * kCooked;
    iso[pvd] = 1;
    std::memcpy(&iso[pvd + 1], "CD001", 5);
    iso[pvd + 6] = 1;
    record(pvd + 156, 18, kCooked, 2, std::string(1, '\0'));
    const std::size_t end = 17 * kCooked;
    iso[end] = 255;
    std::memcpy(&iso[end + 1], "CD001", 5);
    std::size_t at = 18 * kCooked;
    at = record(at, 18, kCooked, 2, std::string(1, '\0'));
    at = record(at, 18, kCooked, 2, std::string(1, '\1'));
    std::uint32_t sector = 19;
    for (const auto& f : kFiles) {
        at = record(at, sector, static_cast<std::uint32_t>(f.text.size()), 0, std::string(f.name) + ";1");
        std::memcpy(&iso[sector * kCooked], f.text.data(), f.text.size());
        ++sector;
    }
    return iso;
}

// The same as whole sectors: sync bytes, the address, mode 1, the data,
// and room for the error codes (left zero).
Bytes rawImage(const Bytes& cooked) {
    Bytes out;
    for (std::size_t s = 0; s < cooked.size() / kCooked; ++s) {
        Bytes sector(kRaw, 0);
        for (int i = 1; i <= 10; ++i) sector[static_cast<std::size_t>(i)] = 0xFF;
        const auto lba = s + 150;
        sector[12] = static_cast<std::uint8_t>(lba / 4500);
        sector[13] = static_cast<std::uint8_t>(lba / 75 % 60);
        sector[14] = static_cast<std::uint8_t>(lba % 75);
        sector[15] = 1;
        std::memcpy(&sector[16], &cooked[s * kCooked], kCooked);
        append(out, sector);
    }
    return out;
}

// The music track: each sample tells where it is.
Bytes audioTrack() {
    Bytes out(kAudioSectors * kRaw);
    for (std::size_t i = 0; i < out.size() / 2; ++i) putLe(out, i * 2, i & 0x7FFF, 2);
    return out;
}

class Folder {
public:
    explicit Folder(const char* name) : path_(std::filesystem::temp_directory_path() / ("hlm-disc-" + std::string(name))) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    std::filesystem::path write(const std::string& name, const Bytes& bytes) const {
        std::ofstream(path_ / name, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path_ / name;
    }
    std::filesystem::path write(const std::string& name, const std::string& text) const { return write(name, Bytes(text.begin(), text.end())); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// What every image of the disc must give back.
void checkDisc(const std::filesystem::path& image, bool withMusic) {
    const auto disc = Disc::open(image);
    const auto* hello = disc->find("hello.txt");
    HLM_CHECK(hello != nullptr);
    if (hello != nullptr) {
        const auto bytes = disc->read(*hello);
        HLM_CHECK(std::string(bytes.begin(), bytes.end()) == "Hello, captain.");
    }
    if (!withMusic) {
        HLM_CHECK(disc->audioTracks().empty());
        return;
    }
    HLM_CHECK(disc->audioTracks().size() == 1);
    if (disc->audioTracks().size() == 1) {
        const auto& track = disc->audioTracks()[0];
        HLM_CHECK(track.number == 2);
        HLM_CHECK(track.size == kAudioSectors * kRaw);
        const auto samples = disc->readAudio(track, 100, 2);
        HLM_CHECK(samples.size() == 4);
        HLM_CHECK(samples.size() == 4 && samples[0] == 200 && samples[3] == 203);
    }
}

// Every shorter copy of an image fails cleanly, or reads what it still has.
void checkCut(const Folder& folder, const std::string& name, const Bytes& bytes) {
    for (std::size_t keep : {std::size_t{0}, std::size_t{7}, bytes.size() / 3, bytes.size() - 13}) {
        const auto path = folder.write(name, Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep)));
        try {
            static_cast<void>(Disc::open(path));
        } catch (const FormatError&) {
        }
    }
}

std::uint32_t crc32(const Bytes& b) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : b) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

} // namespace

HLM_TEST("disc: a plain .iso") {
    Folder f("iso");
    checkDisc(f.write("game.iso", cookedImage()), false);
}

HLM_TEST("disc: a raw .bin alone, and with a cue sheet and a music track") {
    Folder f("cue");
    f.write("data.bin", rawImage(cookedImage()));
    f.write("music.bin", audioTrack());
    checkDisc(f.path() / "data.bin", false);
    f.write("game.cue",
            "FILE \"data.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
            "FILE \"MUSIC.BIN\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n");
    checkDisc(f.path() / "game.cue", true);
}

HLM_TEST("disc: one .bin for all tracks, with a cue sheet or CloneCD") {
    Folder f("one");
    Bytes all = rawImage(cookedImage());
    append(all, audioTrack());
    f.write("game.bin", all);
    f.write("game.cue",
            "FILE \"game.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
            "  TRACK 02 AUDIO\n    INDEX 01 00:00:24\n");
    checkDisc(f.path() / "game.cue", true);
    f.write("game.img", all);
    f.write("game.ccd", "[CloneCD]\nVersion=3\n[TRACK 1]\nMODE=1\nINDEX 1=0\n[TRACK 2]\nMODE=0\nINDEX 1=24\n");
    checkDisc(f.path() / "game.ccd", true);
    checkDisc(f.path() / "game.img", true);
}

HLM_TEST("disc: Alcohol .mds with its .mdf") {
    Folder f("mds");
    Bytes all = rawImage(cookedImage());
    const auto audioAt = all.size();
    append(all, audioTrack());
    f.write("game.mdf", all);
    Bytes mds(0x140, 0);
    std::memcpy(mds.data(), "MEDIA DESCRIPTOR", 16);
    mds[16] = 1;
    mds[17] = 3;
    putLe(mds, 0x14, 1, 2);    // one session
    putLe(mds, 0x50, 0x58, 4); // where it is
    mds[0x58 + 10] = 2;        // two track blocks
    putLe(mds, 0x58 + 20, 0x70, 4);
    for (int i = 0; i < 2; ++i) {
        const std::size_t at = 0x70 + 80 * static_cast<std::size_t>(i);
        mds[at] = i == 0 ? 0xAA : 0xA9; // data, music
        mds[at + 4] = static_cast<std::uint8_t>(i + 1);
        putLe(mds, at + 12, 0x110 + 8 * static_cast<std::size_t>(i), 4);
        putLe(mds, at + 16, kRaw, 2);
        putLe(mds, at + 40, i == 0 ? 0 : audioAt, 8);
        putLe(mds, at + 48, 1, 4);
        putLe(mds, at + 52, 0x120, 4);
        putLe(mds, 0x110 + 8 * static_cast<std::size_t>(i) + 4, i == 0 ? kDataSectors : kAudioSectors, 4);
    }
    putLe(mds, 0x120, 0x130, 4);
    std::memcpy(&mds[0x130], "*.mdf", 5);
    f.write("game.mds", mds);
    checkDisc(f.path() / "game.mds", true);
    checkDisc(f.path() / "game.mdf", true);
    checkCut(f, "game.mds", mds);
}

HLM_TEST("disc: Nero .nrg, disc-at-once and track-at-once") {
    Folder f("nrg");
    {
        Bytes all = rawImage(cookedImage());
        const auto audioAt = all.size();
        append(all, audioTrack());
        const auto chunksAt = all.size();
        Bytes chunk(8 + 22 + 2 * 42, 0);
        std::memcpy(chunk.data(), "DAOX", 4);
        putBe(chunk, 4, chunk.size() - 8, 4);
        chunk[8 + 20] = 1;
        chunk[8 + 21] = 2;
        for (int i = 0; i < 2; ++i) {
            const std::size_t e = 8 + 22 + 42 * static_cast<std::size_t>(i);
            putBe(chunk, e + 12, kRaw, 2);
            chunk[e + 14] = i == 0 ? 0x05 : 0x07;
            const std::uint64_t from = i == 0 ? 0 : audioAt;
            putBe(chunk, e + 18, from, 8);
            putBe(chunk, e + 26, from, 8);
            putBe(chunk, e + 34, i == 0 ? audioAt : chunksAt, 8);
        }
        append(all, chunk);
        append(all, Bytes{'E', 'N', 'D', '!', 0, 0, 0, 0});
        Bytes footer(12, 0);
        std::memcpy(footer.data(), "NER5", 4);
        putBe(footer, 4, chunksAt, 8);
        append(all, footer);
        checkDisc(f.write("dao.nrg", all), true);
        checkCut(f, "dao.nrg", all);
    }
    {
        Bytes all = cookedImage();
        const auto chunksAt = all.size();
        Bytes chunk(8 + 20, 0);
        std::memcpy(chunk.data(), "ETNF", 4);
        putBe(chunk, 4, 20, 4);
        putBe(chunk, 8 + 4, chunksAt, 4); // from 0, this long, mode 0 (2048)
        append(all, chunk);
        append(all, Bytes{'E', 'N', 'D', '!', 0, 0, 0, 0});
        Bytes footer(8, 0);
        std::memcpy(footer.data(), "NERO", 4);
        putBe(footer, 4, chunksAt, 4);
        append(all, footer);
        checkDisc(f.write("tao.nrg", all), false);
    }
}

HLM_TEST("disc: damaged cue sheets and images stay inside their bounds") {
    Folder f("bad");
    f.write("data.bin", rawImage(cookedImage()));
    f.write("a.cue", "FILE \"../../etc/passwd\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    HLM_CHECK_THROWS(Disc::open(f.path() / "a.cue"));
    f.write("b.cue", "FILE \"data.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 99:59:74\n");
    HLM_CHECK_THROWS(Disc::open(f.path() / "b.cue"));
    f.write("c.cue", "TRACK 01 AUDIO\n");
    HLM_CHECK_THROWS(Disc::open(f.path() / "c.cue"));
    checkCut(f, "d.iso", cookedImage());
    checkCut(f, "e.bin", rawImage(cookedImage()));
    HLM_CHECK_THROWS(Disc::open(f.write("f.chd", std::string("MComprHD this is not one"))));
}

HLM_TEST("disc: a game in a .zip, and an image in a folder") {
    Folder f("zip");
    const Bytes iso = cookedImage();
    // A zip with the image kept as it is (no packing).
    const std::string name = "Some Folder/game.iso";
    Bytes zip(30, 0);
    putLe(zip, 0, 0x04034b50, 4);
    putLe(zip, 14, crc32(iso), 4);
    putLe(zip, 18, iso.size(), 4);
    putLe(zip, 22, iso.size(), 4);
    putLe(zip, 26, name.size(), 2);
    zip.insert(zip.end(), name.begin(), name.end());
    append(zip, iso);
    const auto dirAt = zip.size();
    Bytes dir(46, 0);
    putLe(dir, 0, 0x02014b50, 4);
    putLe(dir, 16, crc32(iso), 4);
    putLe(dir, 20, iso.size(), 4);
    putLe(dir, 24, iso.size(), 4);
    putLe(dir, 28, name.size(), 2);
    dir.insert(dir.end(), name.begin(), name.end());
    append(zip, dir);
    Bytes end(22, 0);
    putLe(end, 0, 0x06054b50, 4);
    putLe(end, 8, 1, 2);
    putLe(end, 10, 1, 2);
    putLe(end, 12, dir.size(), 4);
    putLe(end, 16, dirAt, 4);
    append(zip, end);
    GameSource::setCacheFolder(f.path() / "cache");
    const auto source = GameSource::open(f.write("game.zip", zip));
    HLM_CHECK(source != nullptr);
    if (source) {
        HLM_CHECK(source->game() == GameId::JudgmentRites);
        HLM_CHECK(source->exists("HELLO.TXT"));
    }
    // A damaged zip says so.
    Bytes bad = zip;
    bad[30 + name.size() + 19 * kCooked] ^= 1;
    HLM_CHECK_THROWS(GameSource::open(f.write("bad.zip", bad)));
    // An image in a folder of its own.
    std::filesystem::create_directories(f.path() / "cd");
    f.write("cd/game.iso", iso);
    const auto inFolder = GameSource::open(f.path() / "cd");
    HLM_CHECK(inFolder != nullptr && inFolder->exists("DATA.DIR"));
    GameSource::setCacheFolder({});
}
