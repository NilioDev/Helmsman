// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/disc.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <span>
#include <sstream>

namespace helmsman {

namespace {

constexpr std::uint32_t kFirstDescriptor = 16;
// No real folder comes near these; they keep a damaged image from making us
// read without end.
constexpr std::uint32_t kMaxFolderSize = 1u << 20;
constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxFiles = 100000;

bool isSync(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || bytes[0] != 0 || bytes[11] != 0) {
        return false;
    }
    return std::all_of(bytes.begin() + 1, bytes.begin() + 11, [](std::uint8_t b) { return b == 0xFF; });
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::uint64_t fileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw FormatError("cannot read " + path.filename().string());
    }
    return size;
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
    if (!in || c1 != ':' || c2 != ':') {
        throw FormatError("the cue sheet has a bad time");
    }
    return (std::uint64_t{m} * 60 + s) * 75 + f;
}

// A name in a cue sheet, in quotes or not.
std::string cueName(std::istringstream& in) {
    std::string rest;
    std::getline(in, rest);
    const auto first = rest.find('"');
    if (first != std::string::npos) {
        const auto last = rest.find('"', first + 1);
        if (last == std::string::npos) {
            throw FormatError("the cue sheet has a bad file name");
        }
        return rest.substr(first + 1, last - first - 1);
    }
    std::istringstream words(rest);
    std::string name;
    words >> name;
    return name;
}

// Finds a file beside the cue sheet, also when the case of its name differs.
std::filesystem::path besideCue(const std::filesystem::path& cue, const std::string& name) {
    const std::filesystem::path plain(name);
    // Only a bare name is taken: never a path out of the cue's folder.
    if (plain.has_parent_path() || name == ".." || name == "." || name.empty()) {
        throw FormatError("the cue sheet names a file outside its folder");
    }
    // A link to a cue sheet finds the files beside the real one.
    std::error_code real;
    const auto resolved = std::filesystem::canonical(cue, real);
    const auto folder = (real ? cue : resolved).parent_path();
    auto path = folder / plain;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder.empty() ? "." : folder, ec)) {
        if (lower(entry.path().filename().string()) == lower(name)) {
            return entry.path();
        }
    }
    throw FormatError("the cue sheet's file " + name + " is missing");
}

struct CueTrack {
    int number = 0;
    bool audio = false;
    std::uint32_t rawSize = Disc::kRawSector;
    std::filesystem::path file;
    std::uint64_t index1 = 0; // sectors into the file
};

std::vector<CueTrack> readCue(const std::filesystem::path& cue) {
    std::ifstream in(cue, std::ios::binary);
    if (!in) {
        throw FormatError("cannot open the cue sheet");
    }
    std::vector<CueTrack> tracks;
    std::filesystem::path file;
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        if (++lines > 10000) {
            throw FormatError("the cue sheet is too long");
        }
        std::istringstream words(line);
        std::string word;
        words >> word;
        std::transform(word.begin(), word.end(), word.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (word == "FILE") {
            file = besideCue(cue, cueName(words));
        } else if (word == "TRACK") {
            CueTrack track;
            std::string mode;
            words >> track.number >> mode;
            if (file.empty()) {
                throw FormatError("the cue sheet has a track without a file");
            }
            track.file = file;
            track.audio = mode == "AUDIO";
            if (mode == "MODE1/2048") {
                track.rawSize = Disc::kSectorData;
            }
            tracks.push_back(track);
        } else if (word == "INDEX" && !tracks.empty()) {
            int index = 0;
            std::string time;
            words >> index >> time;
            if (index == 1) {
                tracks.back().index1 = cueTime(time);
            }
        }
    }
    if (tracks.empty()) {
        throw FormatError("the cue sheet has no tracks");
    }
    return tracks;
}

} // namespace

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

std::unique_ptr<Disc> Disc::open(const std::filesystem::path& path) {
    auto disc = std::make_unique<Disc>();
    std::uint64_t dataOffset = 0;
    if (lower(path.extension().string()) == ".cue") {
        const auto tracks = readCue(path);
        const CueTrack* data = nullptr;
        for (const auto& track : tracks) {
            if (!track.audio && data == nullptr) {
                data = &track;
            }
        }
        if (data == nullptr) {
            throw FormatError("the CD image has no data track");
        }
        disc->dataPath_ = data->file;
        dataOffset = data->index1 * data->rawSize;
        // Each audio track runs from its index 1 to the next track in the
        // same file, or to the file's end.
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const auto& track = tracks[i];
            if (!track.audio) {
                continue;
            }
            const auto size = fileSize(track.file);
            std::uint64_t end = size;
            if (i + 1 < tracks.size() && tracks[i + 1].file == track.file) {
                end = std::min(size, tracks[i + 1].index1 * kRawSector);
            }
            const auto start = track.index1 * kRawSector;
            if (start < end) {
                disc->audio_.push_back(AudioTrack{track.number, track.file, start, (end - start) / 4 * 4});
            }
        }
    } else {
        disc->dataPath_ = path;
    }
    disc->data_.open(disc->dataPath_, std::ios::binary);
    if (!disc->data_) {
        throw FormatError("cannot open the CD image");
    }
    disc->dataSize_ = fileSize(disc->dataPath_);

    // A raw image's sectors start with the sync bytes; the mode byte says
    // where the data starts.
    if (disc->dataSize_ >= dataOffset + std::uint64_t{kFirstDescriptor + 1} * kRawSector) {
        std::array<std::uint8_t, 24> head{};
        disc->data_.seekg(static_cast<std::streamoff>(dataOffset + kFirstDescriptor * kRawSector));
        disc->data_.read(reinterpret_cast<char*>(head.data()), head.size());
        if (disc->data_ && isSync(head)) {
            disc->rawSize_ = kRawSector;
            disc->dataStart_ = head[15] == 2 ? 24 : 16;
        }
        disc->data_.clear();
    }
    disc->dataStart_ += static_cast<std::uint32_t>(std::min<std::uint64_t>(dataOffset, 0xFFFFFFFFu));

    std::array<std::uint8_t, kSectorData> sector{};
    std::optional<std::uint32_t> root;
    std::uint32_t rootSize = 0;
    for (std::uint32_t s = kFirstDescriptor; s < kFirstDescriptor + 32 && !root; ++s) {
        disc->readSector(s, sector.data());
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
    disc->readFolder(*root, rootSize, "", 0);
    return disc;
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
            const std::uint32_t used = (length + kSectorData - 1) / kSectorData;
            const std::uint64_t end = (std::uint64_t{where} + used - 1) * rawSize_ + dataStart_ + kSectorData;
            if (used > 0 && end > dataSize_) {
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
    const std::uint64_t at = std::uint64_t{sector} * rawSize_ + dataStart_;
    if (at + kSectorData > dataSize_) {
        throw FormatError("the CD image ends too soon");
    }
    data_.seekg(static_cast<std::streamoff>(at));
    data_.read(reinterpret_cast<char*>(out), kSectorData);
    if (!data_) {
        data_.clear();
        throw FormatError("cannot read the CD image");
    }
}

std::vector<std::uint8_t> Disc::read(const DiscFile& file) { return read(file, 0, file.size); }

std::vector<std::uint8_t> Disc::read(const DiscFile& file, std::uint64_t offset, std::size_t size) {
    std::vector<std::uint8_t> out;
    if (offset >= file.size) {
        return out;
    }
    const std::uint64_t end = std::min<std::uint64_t>(file.size, offset + size);
    out.reserve(static_cast<std::size_t>(end - offset));
    if (rawSize_ == kSectorData) {
        // Plain images hold the file in one piece.
        out.resize(static_cast<std::size_t>(end - offset));
        data_.seekg(static_cast<std::streamoff>(std::uint64_t{file.sector} * kSectorData + dataStart_ + offset));
        data_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!data_) {
            data_.clear();
            throw FormatError("cannot read the CD image");
        }
        return out;
    }
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
    const std::uint64_t frames = track.size / 4;
    if (frame >= frames) {
        return out;
    }
    count = static_cast<std::size_t>(std::min<std::uint64_t>(count, frames - frame));
    if (audioOpen_ != track.file) {
        audioFile_.close();
        audioFile_.clear();
        audioFile_.open(track.file, std::ios::binary);
        audioOpen_ = track.file;
    }
    if (!audioFile_) {
        throw FormatError("cannot read the CD image's audio");
    }
    std::vector<std::uint8_t> raw(count * 4);
    audioFile_.seekg(static_cast<std::streamoff>(track.offset + frame * 4));
    audioFile_.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!audioFile_) {
        audioFile_.clear();
        throw FormatError("cannot read the CD image's audio");
    }
    out.resize(count * 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::int16_t>(raw[2 * i] | (raw[2 * i + 1] << 8));
    }
    return out;
}

} // namespace helmsman
