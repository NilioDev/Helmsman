// SPDX-License-Identifier: GPL-3.0-or-later
#include "disc_impl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>

namespace helmsman {

namespace {

constexpr std::uint32_t kFirstDescriptor = 16;
// No real folder comes near these; they keep a damaged image from making us
// read without end.
constexpr std::uint32_t kMaxFolderSize = 1u << 20;
constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxFiles = 100000;
constexpr std::size_t kMaxTracks = 99;
constexpr std::size_t kMaxStores = 200;

bool isSync(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || bytes[0] != 0 || bytes[11] != 0) {
        return false;
    }
    return std::all_of(bytes.begin() + 1, bytes.begin() + 11, [](std::uint8_t b) { return b == 0xFF; });
}

class FileStore final : public Disc::Store {
public:
    explicit FileStore(const std::filesystem::path& path) : in_(path, std::ios::binary), size_(fileSizeOf(path)) {
        if (!in_) {
            throw FormatError("cannot open " + path.filename().string());
        }
    }
    std::uint64_t size() const override { return size_; }
    void read(std::uint64_t at, std::uint8_t* out, std::size_t n) override {
        if (at > size_ || n > size_ - at) {
            throw FormatError("the CD image ends too soon");
        }
        in_.seekg(static_cast<std::streamoff>(at));
        in_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
        if (!in_) {
            in_.clear();
            throw FormatError("cannot read the CD image");
        }
    }

private:
    std::ifstream in_;
    std::uint64_t size_;
};

} // namespace

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::uint64_t fileSizeOf(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw FormatError("cannot read " + path.filename().string());
    }
    return size;
}

std::filesystem::path findBeside(const std::filesystem::path& near, const std::string& name) {
    const std::filesystem::path plain(name);
    // Only a bare name is taken: never a path out of the folder.
    if (plain.has_parent_path() || plain.has_root_path() || name == ".." || name == "." || name.empty() ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return {};
    }
    // A link to an image finds the files beside the real one.
    std::error_code real;
    const auto resolved = std::filesystem::canonical(near, real);
    const auto folder = (real ? near : resolved).parent_path();
    auto path = folder / plain;
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        return path;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder.empty() ? "." : folder, ec)) {
        if (lowerText(entry.path().filename().string()) == lowerText(name) && entry.is_regular_file(ec)) {
            return entry.path();
        }
    }
    return {};
}

std::filesystem::path besideFile(const std::filesystem::path& near, const std::string& name) {
    const std::filesystem::path plain(name);
    if (plain.has_parent_path() || plain.has_root_path() || name == ".." || name == "." || name.empty()) {
        throw FormatError("the image names a file outside its folder");
    }
    auto found = findBeside(near, name);
    if (found.empty()) {
        throw FormatError("the image's file " + name + " is missing");
    }
    return found;
}

std::unique_ptr<Disc::Store> fileStore(const std::filesystem::path& path) { return std::make_unique<FileStore>(path); }

std::size_t DiscBuilder::addStore(std::unique_ptr<Disc::Store> store) {
    if (disc.stores_.size() >= kMaxStores) {
        throw FormatError("the CD image has too many files");
    }
    disc.stores_.push_back(std::move(store));
    return disc.stores_.size() - 1;
}

void DiscBuilder::addTrack(const Disc::Track& track) {
    if (disc.tracks_.size() >= kMaxTracks) {
        throw FormatError("the CD image has too many tracks");
    }
    if (track.store >= disc.stores_.size() || track.stride == 0 || track.stride > 4096 || track.skip >= track.stride) {
        throw FormatError("the CD image has a damaged track");
    }
    disc.tracks_.push_back(track);
}

std::string normalPath(std::string_view path) {
    std::string out;
    for (const char c : path) {
        out.push_back(c == '\\' ? '/' : static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    while (!out.empty() && out.front() == '/') {
        out.erase(out.begin());
    }
    return out;
}

Disc::Disc() = default;
Disc::~Disc() = default;

bool Disc::isImage(const std::filesystem::path& path) {
    static const char* const kKinds[] = {".cue", ".bin", ".iso", ".img", ".ccd", ".mds", ".mdf",
                                         ".nrg", ".chd", ".gog", ".ins", ".inst"};
    const auto ext = lowerText(path.extension().string());
    return std::any_of(std::begin(kKinds), std::end(kKinds), [&](const char* k) { return ext == k; });
}

std::unique_ptr<Disc> Disc::open(const std::filesystem::path& path) {
    auto disc = std::make_unique<Disc>();
    DiscBuilder b{*disc};
    const auto ext = lowerText(path.extension().string());
    const auto stem = path.stem().string();
    if (ext == ".cue" || ext == ".ins" || ext == ".inst") {
        readCueSheet(b, path);
    } else if (ext == ".ccd") {
        readCloneCd(b, path);
    } else if (ext == ".mds") {
        readAlcohol(b, path);
    } else if (ext == ".nrg") {
        readNero(b, path);
    } else if (ext == ".chd") {
        readChd(b, path);
    } else if (const auto ccd = findBeside(path, stem + ".ccd"); ext == ".img" && !ccd.empty()) {
        readCloneCd(b, ccd);
    } else if (const auto mds = findBeside(path, stem + ".mds"); ext == ".mdf" && !mds.empty()) {
        readAlcohol(b, mds);
    } else {
        // One data track, its sectors cooked or raw (found below).
        Track t;
        t.number = 1;
        t.store = b.addStore(fileStore(path));
        t.stride = 0;
        disc->tracks_.push_back(t);
    }
    disc->finish();
    return disc;
}

void Disc::finish() {
    const auto data = std::find_if(tracks_.begin(), tracks_.end(), [](const Track& t) { return !t.audio; });
    if (data == tracks_.end()) {
        throw FormatError("the CD image has no data track");
    }
    data_ = *data;
    Store& store = *stores_[data_.store];
    // A raw sector starts with the sync bytes; the mode byte says where the
    // data starts. Where the reader did not know the size of a sector, a
    // raw one has them, and a cooked one is 2048 bytes.
    const std::uint32_t guess = data_.stride == 0 ? kRawSector : data_.stride;
    if (guess >= kRawSector) {
        const std::uint64_t at = data_.start + std::uint64_t{kFirstDescriptor} * guess;
        std::array<std::uint8_t, 24> head{};
        bool raw = false;
        if (store.size() >= at + head.size()) {
            store.read(at, head.data(), head.size());
            raw = isSync(head);
        }
        if (raw) {
            data_.stride = guess;
            data_.skip = head[15] == 2 ? 24 : 16;
        } else if (data_.stride == 0) {
            data_.stride = kSectorData;
        }
    }
    if (store.size() < data_.start) {
        throw FormatError("the CD image ends too soon");
    }
    dataSectors_ = (store.size() - data_.start) / data_.stride;
    if (data_.sectors != 0) {
        dataSectors_ = std::min(dataSectors_, data_.sectors);
    }

    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        const Track& t = tracks_[i];
        if (!t.audio) {
            continue;
        }
        const std::uint64_t have = stores_[t.store]->size();
        if (t.start >= have) {
            continue;
        }
        std::uint64_t bytes = 0;
        if (t.stride == kRawSector) {
            bytes = have - t.start;
            if (t.sectors != 0) {
                bytes = std::min(bytes, t.sectors * kRawSector);
            }
        } else if (t.stride > kRawSector) {
            std::uint64_t sectors = (have - t.start) / t.stride;
            if (t.sectors != 0) {
                sectors = std::min(sectors, t.sectors);
            }
            bytes = sectors * kRawSector;
        }
        bytes = bytes / 4 * 4;
        if (bytes > 0) {
            audio_.push_back(AudioTrack{t.number, i, bytes});
        }
    }

    std::array<std::uint8_t, kSectorData> sector{};
    std::optional<std::uint32_t> root;
    std::uint32_t rootSize = 0;
    for (std::uint32_t s = kFirstDescriptor; s < kFirstDescriptor + 32 && !root; ++s) {
        readSector(s, sector.data());
        if (std::memcmp(sector.data() + 1, "CD001", 5) != 0) {
            throw FormatError("not a CD image");
        }
        if (sector[0] == 255) {
            break;
        }
        if (sector[0] == 1) {
            Reader r(sector);
            r.seek(156 + 2);
            root = r.u32();
            r.seek(156 + 10);
            rootSize = r.u32();
        }
    }
    if (!root) {
        throw FormatError("the CD image has no file system");
    }
    readFolder(*root, rootSize, "", 0);
}

void Disc::readFolder(std::uint32_t at, std::uint32_t size, const std::string& prefix, int depth) {
    if (size > kMaxFolderSize || depth > kMaxDepth) {
        throw FormatError("a folder on the CD image is damaged");
    }
    std::array<std::uint8_t, kSectorData> sector{};
    struct Sub {
        std::string path;
        std::uint32_t at;
        std::uint32_t size;
    };
    std::vector<Sub> subs;
    const std::uint32_t sectors = (size + kSectorData - 1) / kSectorData;
    for (std::uint32_t i = 0; i < sectors; ++i) {
        readSector(at + i, sector.data());
        std::size_t pos = 0;
        while (pos < kSectorData && sector[pos] != 0) {
            Reader r(std::span<const std::uint8_t>(sector).subspan(pos, std::min<std::size_t>(sector[pos], kSectorData - pos)));
            r.skip(2);
            const std::uint32_t where = r.u32();
            r.skip(4);
            const std::uint32_t length = r.u32();
            r.skip(25 - 14);
            const std::uint8_t flags = r.u8();
            r.skip(32 - 26);
            const std::uint8_t nameLength = r.u8();
            const auto name = r.bytes(nameLength);
            pos += sector[pos];
            if (nameLength <= 1 && (name.empty() || name[0] <= 1)) {
                continue; // this folder and the one above
            }
            std::string text(name.begin(), name.end());
            if (const auto semicolon = text.find(';'); semicolon != std::string::npos) {
                text.resize(semicolon);
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            if (text.find('/') != std::string::npos || text.find('\\') != std::string::npos || text == "..") {
                continue;
            }
            const std::string full = prefix + normalPath(text);
            if ((flags & 0x02) != 0) {
                subs.push_back(Sub{full + "/", where, length});
                continue;
            }
            const std::uint64_t used = (std::uint64_t{length} + kSectorData - 1) / kSectorData;
            if (used > 0 && std::uint64_t{where} + used > dataSectors_) {
                throw FormatError("a file on the CD image goes past its end");
            }
            if (files_.size() >= kMaxFiles) {
                throw FormatError("the CD image has too many files");
            }
            files_.push_back(DiscFile{full, where, length});
        }
    }
    for (const auto& sub : subs) {
        readFolder(sub.at, sub.size, sub.path, depth + 1);
    }
}

const DiscFile* Disc::find(std::string_view path) const {
    const std::string wanted = normalPath(path);
    for (const auto& file : files_) {
        if (file.path == wanted) {
            return &file;
        }
    }
    return nullptr;
}

void Disc::readSector(std::uint32_t sector, std::uint8_t* out) {
    if (sector >= dataSectors_) {
        throw FormatError("the CD image ends too soon");
    }
    stores_[data_.store]->read(data_.start + std::uint64_t{sector} * data_.stride + data_.skip, out, kSectorData);
}

std::vector<std::uint8_t> Disc::read(const DiscFile& file) { return read(file, 0, file.size); }

std::vector<std::uint8_t> Disc::read(const DiscFile& file, std::uint64_t offset, std::size_t size) {
    std::vector<std::uint8_t> out;
    if (offset >= file.size) {
        return out;
    }
    const std::uint64_t end = std::min<std::uint64_t>(file.size, offset + size);
    if (data_.stride == kSectorData) {
        // Cooked images hold the file in one piece.
        out.resize(static_cast<std::size_t>(end - offset));
        if (std::uint64_t{file.sector} * kSectorData + end > dataSectors_ * kSectorData) {
            throw FormatError("the CD image ends too soon");
        }
        stores_[data_.store]->read(data_.start + std::uint64_t{file.sector} * kSectorData + offset, out.data(), out.size());
        return out;
    }
    out.reserve(static_cast<std::size_t>(end - offset));
    std::array<std::uint8_t, kSectorData> sector{};
    for (std::uint64_t at = offset; at < end;) {
        const auto index = static_cast<std::uint32_t>(at / kSectorData);
        readSector(file.sector + index, sector.data());
        const auto from = static_cast<std::size_t>(at % kSectorData);
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(kSectorData - from, end - at));
        out.insert(out.end(), sector.begin() + static_cast<std::ptrdiff_t>(from),
                   sector.begin() + static_cast<std::ptrdiff_t>(from + take));
        at += take;
    }
    return out;
}

std::vector<std::int16_t> Disc::readAudio(const AudioTrack& track, std::uint64_t frame, std::size_t count) {
    std::vector<std::int16_t> out;
    if (track.index >= tracks_.size()) {
        return out;
    }
    const Track& t = tracks_[track.index];
    const std::uint64_t frames = track.size / 4;
    if (frame >= frames) {
        return out;
    }
    count = static_cast<std::size_t>(std::min<std::uint64_t>(count, frames - frame));
    std::vector<std::uint8_t> raw(count * 4);
    Store& store = *stores_[t.store];
    if (t.stride == kRawSector) {
        store.read(t.start + frame * 4, raw.data(), raw.size());
    } else {
        // Sectors with more than the samples (the subchannel): a piece of
        // each.
        std::uint64_t at = frame * 4;
        std::size_t done = 0;
        while (done < raw.size()) {
            const std::uint64_t sector = at / kRawSector;
            const auto from = static_cast<std::size_t>(at % kRawSector);
            const std::size_t take = std::min<std::size_t>(kRawSector - from, raw.size() - done);
            store.read(t.start + sector * t.stride + from, raw.data() + done, take);
            done += take;
            at += take;
        }
    }
    out.resize(count * 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint8_t lo = t.bigEndian ? raw[2 * i + 1] : raw[2 * i];
        const std::uint8_t hi = t.bigEndian ? raw[2 * i] : raw[2 * i + 1];
        out[i] = static_cast<std::int16_t>(lo | (hi << 8));
    }
    return out;
}

} // namespace helmsman
