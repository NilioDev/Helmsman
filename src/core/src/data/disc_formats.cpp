// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kinds of disc image: what each one says about the CD's tracks, and
// where their sectors lie. See docs/formats/disc-images.md.
#include "disc_impl.hpp"

#include <libchdr/chd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace helmsman {

namespace {

constexpr std::uint32_t kRaw = Disc::kRawSector;
constexpr std::uint32_t kCooked = Disc::kSectorData;
constexpr std::size_t kMaxSheet = 1u << 20; // a cue sheet or .ccd is a few KB

std::string upperText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string readSheet(const std::filesystem::path& path) {
    if (fileSizeOf(path) > kMaxSheet) {
        throw FormatError("the image's description is too long");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw FormatError("cannot open " + path.filename().string());
    }
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

std::vector<std::uint8_t> readWhole(const std::filesystem::path& path, std::size_t most) {
    const auto size = fileSizeOf(path);
    if (size > most) {
        throw FormatError("the image's description is too long");
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in) {
        throw FormatError("cannot read " + path.filename().string());
    }
    return out;
}

// "MM:SS:FF" in frames of a 75th of a second, which are sectors.
std::uint64_t cueTime(const std::string& text) {
    unsigned m = 0;
    unsigned s = 0;
    unsigned f = 0;
    char c1 = 0;
    char c2 = 0;
    std::istringstream in(text);
    in >> m >> c1 >> s >> c2 >> f;
    if (!in || c1 != ':' || c2 != ':' || s >= 60 || f >= 75 || m > 6000) {
        throw FormatError("the cue sheet has a bad time");
    }
    return (std::uint64_t{m} * 60 + s) * 75 + f;
}

// A name in a cue sheet, in quotes or not, and what follows it.
std::pair<std::string, std::string> cueName(std::istringstream& in) {
    std::string rest;
    std::getline(in, rest);
    const auto first = rest.find('"');
    if (first != std::string::npos) {
        const auto last = rest.find('"', first + 1);
        if (last == std::string::npos) {
            throw FormatError("the cue sheet has a bad file name");
        }
        return {rest.substr(first + 1, last - first - 1), rest.substr(last + 1)};
    }
    std::istringstream words(rest);
    std::string name;
    std::string type;
    words >> name >> type;
    return {name, type};
}

std::string firstWord(const std::string& text) {
    std::istringstream in(text);
    std::string word;
    in >> word;
    return upperText(word);
}

// Sector size and where the data starts in one, for a cue sheet's mode.
std::pair<std::uint32_t, std::uint32_t> cueMode(const std::string& mode) {
    if (mode == "AUDIO" || mode == "MODE1/2352" || mode == "MODE2/2352" || mode == "CDI/2352") {
        return {kRaw, 0}; // where data starts is found from the sector
    }
    if (mode == "MODE1/2048" || mode == "MODE2/2048") {
        return {kCooked, 0};
    }
    if (mode == "MODE2/2336" || mode == "CDI/2336") {
        return {2336, 8};
    }
    if (mode == "CDG") {
        return {2448, 0};
    }
    throw FormatError("the cue sheet has a track of an unknown kind: " + mode);
}

} // namespace

void readCueSheet(DiscBuilder& b, const std::filesystem::path& cue) {
    struct Entry {
        Disc::Track track;
        std::size_t file = 0;
        bool music = false;
        bool haveIndex = false;
        std::uint64_t index1 = 0; // sectors into its file
    };
    struct File {
        std::filesystem::path path;
        std::string type;
        std::size_t store = 0;
        bool opened = false;
    };
    std::vector<File> files;
    std::vector<Entry> entries;
    std::istringstream in(readSheet(cue));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string word;
        words >> word;
        word = upperText(word);
        if (word == "FILE") {
            auto [name, rest] = cueName(words);
            files.push_back(File{besideFile(cue, name), firstWord(rest)});
        } else if (word == "TRACK") {
            if (files.empty()) {
                throw FormatError("the cue sheet has a track without a file");
            }
            Entry e;
            std::string mode;
            words >> e.track.number >> mode;
            mode = upperText(mode);
            e.track.audio = mode == "AUDIO";
            e.file = files.size() - 1;
            e.music = isMusicFile(files.back().path, files.back().type);
            const auto [stride, skip] = cueMode(mode);
            e.track.stride = e.music ? kRaw : stride;
            e.track.skip = skip;
            e.track.bigEndian = files.back().type == "MOTOROLA";
            entries.push_back(e);
        } else if (word == "INDEX" && !entries.empty()) {
            int index = 0;
            std::string time;
            words >> index >> time;
            if (index == 1) {
                entries.back().index1 = cueTime(time);
                entries.back().haveIndex = true;
            }
        }
    }
    if (entries.empty()) {
        throw FormatError("the cue sheet has no tracks");
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& e = entries[i];
        File& f = files[e.file];
        if (!f.opened) {
            f.store = b.addStore(e.music ? musicStore(f.path) : fileStore(f.path));
            f.opened = true;
        }
        Disc::Track t = e.track;
        t.store = f.store;
        t.start = e.index1 * t.stride;
        // A track runs to the next one in the same file, or to its end.
        if (i + 1 < entries.size() && entries[i + 1].file == e.file && entries[i + 1].index1 > e.index1) {
            t.sectors = entries[i + 1].index1 - e.index1;
        }
        b.addTrack(t);
    }
}

void readCloneCd(DiscBuilder& b, const std::filesystem::path& ccd) {
    // An INI file: [TRACK n] sections with MODE= (0 audio) and INDEX 1=
    // (the sector the track starts at in the .img beside it).
    struct Entry {
        int mode = 1;
        std::int64_t index1 = -1;
    };
    std::map<int, Entry> tracks;
    std::istringstream in(readSheet(ccd));
    std::string line;
    int current = -1;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            const std::string head = upperText(line);
            current = -1;
            if (head.rfind("[TRACK ", 0) == 0) {
                current = std::atoi(head.c_str() + 7);
                if (current < 1 || current > 99) {
                    throw FormatError("the .ccd has a bad track number");
                }
                tracks[current];
            }
            continue;
        }
        const auto eq = line.find('=');
        if (current < 0 || eq == std::string::npos) {
            continue;
        }
        const std::string key = upperText(line.substr(0, eq));
        const std::string value = line.substr(eq + 1);
        if (key == "MODE") {
            tracks[current].mode = std::atoi(value.c_str());
        } else if (key == "INDEX 1") {
            tracks[current].index1 = std::atoll(value.c_str());
        }
    }
    if (tracks.empty()) {
        throw FormatError("the .ccd has no tracks");
    }
    const auto image = besideFile(ccd, ccd.stem().string() + ".img");
    const std::size_t store = b.addStore(fileStore(image));
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        if (it->second.index1 < 0 || it->second.index1 > 450000) {
            throw FormatError("the .ccd has a track without a start");
        }
        Disc::Track t;
        t.number = it->first;
        t.audio = it->second.mode == 0;
        t.store = store;
        t.stride = kRaw;
        t.start = static_cast<std::uint64_t>(it->second.index1) * kRaw;
        if (const auto next = std::next(it); next != tracks.end() && next->second.index1 > it->second.index1) {
            t.sectors = static_cast<std::uint64_t>(next->second.index1 - it->second.index1);
        }
        b.addTrack(t);
    }
}

void readAlcohol(DiscBuilder& b, const std::filesystem::path& mds) {
    const auto bytes = readWhole(mds, 16u << 20);
    Reader r(bytes);
    if (bytes.size() < 0x58 || std::memcmp(bytes.data(), "MEDIA DESCRIPTOR", 16) != 0) {
        throw FormatError("not an Alcohol .mds file");
    }
    if (bytes[16] != 1) {
        throw FormatError("this kind of .mds file is not known yet (only version 1)");
    }
    r.seek(0x14);
    const std::uint16_t sessions = r.u16();
    r.seek(0x50);
    const std::uint32_t sessionAt = r.u32();
    if (sessions == 0 || sessions > 99) {
        throw FormatError("the .mds file has no sessions");
    }
    std::filesystem::path data;
    std::size_t store = 0;
    for (std::uint16_t s = 0; s < sessions; ++s) {
        r.seek(sessionAt + s * 24u + 10);
        const std::uint8_t blocks = r.u8();
        r.seek(sessionAt + s * 24u + 20);
        const std::uint32_t blockAt = r.u32();
        for (std::uint8_t i = 0; i < blocks; ++i) {
            const std::uint32_t at = blockAt + i * 80u;
            r.seek(at);
            const std::uint8_t mode = r.u8();
            r.seek(at + 4);
            const std::uint8_t point = r.u8();
            if (point < 1 || point > 99) {
                continue; // the lead-in's entries
            }
            r.seek(at + 12);
            const std::uint32_t extraAt = r.u32();
            const std::uint16_t sectorSize = r.u16();
            r.seek(at + 40);
            const std::uint64_t start = r.u32() | (std::uint64_t{r.u32()} << 32);
            const std::uint32_t names = r.u32();
            const std::uint32_t nameAt = r.u32();
            std::uint32_t length = 0;
            if (extraAt != 0) {
                r.seek(extraAt + 4);
                length = r.u32();
            }
            if (data.empty()) {
                if (names == 0) {
                    throw FormatError("the .mds file names no image");
                }
                r.seek(nameAt);
                const std::uint32_t textAt = r.u32();
                const bool wide = r.u8() != 0;
                std::string name;
                r.seek(textAt);
                for (int n = 0; n < 512; ++n) {
                    const std::uint16_t c = wide ? r.u16() : r.u8();
                    if (c == 0) {
                        break;
                    }
                    name.push_back(c < 0x80 ? static_cast<char>(c) : '_');
                }
                // "*.mdf": the same name as the .mds.
                if (name.size() > 1 && name[0] == '*') {
                    name = mds.stem().string() + name.substr(1);
                }
                data = besideFile(mds, name);
                store = b.addStore(fileStore(data));
            }
            Disc::Track t;
            t.number = point;
            t.audio = (mode & 0x0F) == 0x09;
            t.store = store;
            t.stride = sectorSize;
            t.start = start;
            t.sectors = length;
            b.addTrack(t);
        }
    }
}

void readNero(DiscBuilder& b, const std::filesystem::path& nrg) {
    // The footer at the file's end points at a list of chunks, each a
    // four-letter name and a size, with the numbers high byte first.
    std::ifstream in(nrg, std::ios::binary);
    const auto size = fileSizeOf(nrg);
    if (!in || size < 12) {
        throw FormatError("not a Nero image");
    }
    auto readAt = [&](std::uint64_t at, std::size_t n) {
        if (at > size || n > size - at) {
            throw FormatError("the Nero image is damaged");
        }
        std::vector<std::uint8_t> out(n);
        in.seekg(static_cast<std::streamoff>(at));
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
        if (!in) {
            throw FormatError("cannot read the Nero image");
        }
        return out;
    };
    auto be = [](const std::uint8_t* p, int n) {
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = v << 8 | p[i];
        }
        return v;
    };
    std::uint64_t chunkAt = 0;
    bool v2 = false;
    const auto tail = readAt(size - 12, 12);
    if (std::memcmp(tail.data(), "NER5", 4) == 0) {
        chunkAt = be(tail.data() + 4, 8);
        v2 = true;
    } else if (std::memcmp(tail.data() + 4, "NERO", 4) == 0) {
        chunkAt = be(tail.data() + 8, 4);
    } else {
        throw FormatError("not a Nero image");
    }
    const std::size_t store = b.addStore(fileStore(nrg));
    bool any = false;
    for (int guard = 0; guard < 1000 && chunkAt + 8 <= size; ++guard) {
        const auto head = readAt(chunkAt, 8);
        const std::string id(head.begin(), head.begin() + 4);
        const auto length = be(head.data() + 4, 4);
        if (id == "END!") {
            break;
        }
        if (length > (1u << 20)) {
            throw FormatError("the Nero image is damaged");
        }
        const auto body = readAt(chunkAt + 8, static_cast<std::size_t>(length));
        if ((id == "DAOX" || id == "DAOI") && !any) {
            // A header of 22 bytes (the catalogue number among it), then one
            // entry per track: an ISRC, the sector size, the mode, and where
            // index 0, index 1 and the end lie in the file. The tracks count
            // up from 1 in the order of the entries.
            const bool wide = id == "DAOX";
            const std::size_t entry = wide ? 42 : 30;
            if (body.size() < 22 + entry) {
                throw FormatError("the Nero image is damaged");
            }
            const int count = static_cast<int>(std::min<std::size_t>((body.size() - 22) / entry, 99));
            for (int n = 1; n <= count; ++n) {
                const std::uint8_t* e = body.data() + 22 + entry * static_cast<std::size_t>(n - 1);
                const auto sectorSize = static_cast<std::uint32_t>(be(e + 12, 2));
                const std::uint8_t mode = e[14];
                const int w = wide ? 8 : 4;
                const auto index1 = be(e + 18 + w, w);
                const auto end = be(e + 18 + 2 * w, w);
                if (sectorSize == 0 || end < index1) {
                    throw FormatError("the Nero image is damaged");
                }
                Disc::Track t;
                t.number = n;
                t.audio = mode == 0x07 || mode == 0x10;
                t.store = store;
                t.stride = sectorSize;
                t.skip = sectorSize == 2336 ? 8 : 0;
                t.start = index1;
                t.sectors = (end - index1) / sectorSize;
                b.addTrack(t);
            }
            any = true;
        } else if ((id == "ETN2" || id == "ETNF") && !any) {
            // Track-at-once: where each track lies and its mode.
            const std::size_t entry = id == "ETN2" ? 32 : 20;
            const int w = id == "ETN2" ? 8 : 4;
            for (std::size_t k = 0; k + entry <= body.size(); k += entry) {
                const std::uint8_t* e = body.data() + k;
                const auto offset = be(e, w);
                const auto bytes = be(e + w, w);
                // The mode is the last byte of the next four; it alone says
                // how large a sector is.
                const std::uint8_t mode = e[2 * w + 3];
                std::uint32_t sectorSize = kRaw;
                if (mode == 0x00 || mode == 0x02) {
                    sectorSize = kCooked;
                } else if (mode == 0x03) {
                    sectorSize = 2336;
                } else if (mode == 0x0F || mode == 0x10 || mode == 0x11) {
                    sectorSize = 2448;
                }
                Disc::Track t;
                t.number = static_cast<int>(k / entry) + 1;
                t.audio = mode == 0x07 || mode == 0x10;
                t.store = store;
                t.stride = sectorSize;
                t.skip = sectorSize == 2336 ? 8 : 0;
                t.start = offset;
                t.sectors = bytes / sectorSize;
                b.addTrack(t);
            }
            any = true;
        }
        chunkAt += 8 + length;
    }
    (void)v2;
    if (!any) {
        throw FormatError("the Nero image has no tracks");
    }
}

namespace {

// libchdr reads the file through these, so that names in any script work.
struct ChdFile {
    std::ifstream in;
    std::uint64_t size = 0;
};

std::uint64_t chdSize(void* p) { return static_cast<ChdFile*>(p)->size; }
std::size_t chdRead(void* out, std::size_t size, std::size_t count, void* p) {
    auto& f = *static_cast<ChdFile*>(p);
    if (size == 0) {
        return 0;
    }
    f.in.read(static_cast<char*>(out), static_cast<std::streamsize>(size * count));
    const auto got = static_cast<std::size_t>(std::max<std::streamsize>(f.in.gcount(), 0));
    f.in.clear();
    return got / size;
}
int chdClose(void* p) {
    delete static_cast<ChdFile*>(p);
    return 0;
}
int chdSeek(void* p, std::int64_t offset, int whence) {
    auto& f = *static_cast<ChdFile*>(p);
    f.in.clear();
    const auto dir = whence == SEEK_SET ? std::ios::beg : whence == SEEK_CUR ? std::ios::cur : std::ios::end;
    f.in.seekg(static_cast<std::streamoff>(offset), dir);
    return f.in ? 0 : -1;
}
const core_file_callbacks kChdCallbacks = {chdSize, chdRead, chdClose, chdSeek};

class ChdStore final : public Disc::Store {
public:
    explicit ChdStore(const std::filesystem::path& path) {
        auto* file = new ChdFile;
        file->in.open(path, std::ios::binary);
        if (!file->in) {
            delete file;
            throw FormatError("cannot open " + path.filename().string());
        }
        file->size = fileSizeOf(path);
        // libchdr closes the file itself, also when it cannot open it.
        if (chd_open_core_file_callbacks(&kChdCallbacks, file, CHD_OPEN_READ, nullptr, &chd_) != CHDERR_NONE) {
            chd_ = nullptr;
            throw FormatError("not a .chd image, or a damaged one");
        }
        const chd_header* h = chd_get_header(chd_);
        hunkBytes_ = h->hunkbytes;
        hunks_ = h->totalhunks;
        if (hunkBytes_ == 0 || hunkBytes_ > (1u << 24)) {
            throw FormatError("the .chd image is damaged");
        }
        hunk_.resize(hunkBytes_);
    }
    ~ChdStore() override {
        if (chd_ != nullptr) {
            chd_close(chd_);
        }
    }
    ChdStore(const ChdStore&) = delete;
    ChdStore& operator=(const ChdStore&) = delete;

    std::uint64_t size() const override { return std::uint64_t{hunks_} * hunkBytes_; }
    void read(std::uint64_t at, std::uint8_t* out, std::size_t n) override {
        if (at > size() || n > size() - at) {
            throw FormatError("the CD image ends too soon");
        }
        while (n > 0) {
            const auto hunk = static_cast<std::uint32_t>(at / hunkBytes_);
            if (hunk != cached_) {
                if (chd_read(chd_, hunk, hunk_.data()) != CHDERR_NONE) {
                    cached_ = ~0u;
                    throw FormatError("cannot read the .chd image (damaged?)");
                }
                cached_ = hunk;
            }
            const auto from = static_cast<std::size_t>(at % hunkBytes_);
            const std::size_t take = std::min<std::size_t>(n, hunkBytes_ - from);
            std::memcpy(out, hunk_.data() + from, take);
            out += take;
            n -= take;
            at += take;
        }
    }
    chd_file* chd() { return chd_; }

private:
    chd_file* chd_ = nullptr;
    std::uint32_t hunkBytes_ = 0;
    std::uint32_t hunks_ = 0;
    std::vector<std::uint8_t> hunk_;
    std::uint32_t cached_ = ~0u;
};

} // namespace

void readChd(DiscBuilder& b, const std::filesystem::path& path) {
    auto owned = std::make_unique<ChdStore>(path);
    ChdStore& chd = *owned;
    const std::size_t store = b.addStore(std::move(owned));
    // A frame is a sector of 2352 bytes and its 96 of subchannel; each
    // track's frames are rounded up to a multiple of four.
    constexpr std::uint32_t kFrame = 2448;
    std::uint64_t frame = 0;
    for (std::uint32_t i = 0; i < 99; ++i) {
        std::array<char, 256> text{};
        std::uint32_t got = 0;
        bool v2 = true;
        if (chd_get_metadata(chd.chd(), CDROM_TRACK_METADATA2_TAG, i, text.data(), text.size() - 1, &got, nullptr, nullptr) !=
            CHDERR_NONE) {
            v2 = false;
            if (chd_get_metadata(chd.chd(), CDROM_TRACK_METADATA_TAG, i, text.data(), text.size() - 1, &got, nullptr, nullptr) !=
                CHDERR_NONE) {
                break;
            }
        }
        text[std::min<std::size_t>(got, text.size() - 1)] = 0;
        int number = 0;
        int frames = 0;
        int pregap = 0;
        int postgap = 0;
        std::array<char, 32> type{};
        std::array<char, 32> sub{};
        std::array<char, 32> pgType{};
        std::array<char, 32> pgSub{};
        int fields = 0;
        if (v2) {
            fields = std::sscanf(text.data(), "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
                                 &number, type.data(), sub.data(), &frames, &pregap, pgType.data(), pgSub.data(), &postgap);
        } else {
            fields = std::sscanf(text.data(), "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &number, type.data(), sub.data(), &frames);
        }
        if (fields < 4 || frames < 0 || frames > 450000 || pregap < 0 || pregap > frames) {
            throw FormatError("the .chd image's track list is damaged");
        }
        const std::string kind = type.data();
        Disc::Track t;
        t.number = number;
        t.audio = kind == "AUDIO";
        t.store = store;
        t.stride = kFrame;
        t.bigEndian = true; // CHD keeps CD audio high byte first
        // Data in a cooked track sits at the frame's start; a raw one's
        // layout is found from its sync bytes.
        if (kind == "MODE2" || kind == "MODE2_FORM_MIX") {
            t.skip = 8;
        }
        const int lead = pgType[0] == 'V' ? pregap : 0;
        t.start = (frame + static_cast<std::uint64_t>(lead)) * kFrame;
        t.sectors = static_cast<std::uint64_t>(frames - lead);
        b.addTrack(t);
        frame += (static_cast<std::uint64_t>(frames) + 3) / 4 * 4;
    }
    if (b.trackCount() == 0) {
        throw FormatError("the .chd image is not of a CD");
    }
}

} // namespace helmsman
