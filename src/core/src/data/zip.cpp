// SPDX-License-Identifier: GPL-3.0-or-later
//
// A zip archive ends with a directory of its files: the "end record" (its
// last 22 bytes, plus a comment of up to 64 KiB) says where that directory
// starts and how many entries it has; each entry names a file, how it is
// packed, its sizes and checksum, and where its own header sits. Archives
// larger than 4 GiB, or with very many files, carry 64-bit values in an
// extra end record and in an extra field of each entry. docs/formats/
// disc-images.md describes the parts Helmsman reads.
#include "zip.hpp"

#include "helmsman/data/reader.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace helmsman {

namespace {

constexpr std::uint64_t kMaxTotal = 8ULL << 30; // all files together
constexpr std::size_t kMaxEntries = 20000;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The archive's bytes; a split archive's parts one after another.
class Parts {
public:
    explicit Parts(const std::filesystem::path& first) {
        const auto name = first.filename().string();
        const bool split = name.size() > 4 && name.compare(name.size() - 4, 4, ".001") == 0;
        std::error_code ec;
        if (!split) {
            add(first);
            return;
        }
        const auto base = name.substr(0, name.size() - 3);
        for (int n = 1; n < 1000; ++n) {
            char number[8];
            std::snprintf(number, sizeof number, "%03d", n);
            const auto part = first.parent_path() / (base + number);
            if (!std::filesystem::is_regular_file(part, ec)) {
                break;
            }
            add(part);
        }
    }

    std::uint64_t size() const { return size_; }

    void read(std::uint64_t at, std::uint8_t* out, std::size_t n) {
        if (at > size_ || n > size_ - at) {
            throw FormatError("the zip archive ends early");
        }
        while (n > 0) {
            std::size_t i = 0;
            while (at >= starts_[i] + sizes_[i]) {
                ++i;
            }
            const auto within = at - starts_[i];
            const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(n, sizes_[i] - within));
            if (open_ != i) {
                file_.close();
                file_.clear();
                file_.open(paths_[i], std::ios::binary);
                open_ = i;
            }
            file_.seekg(static_cast<std::streamoff>(within));
            file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(take));
            if (!file_) {
                throw FormatError("cannot read " + paths_[i].filename().string());
            }
            out += take;
            at += take;
            n -= take;
        }
    }

    std::vector<std::uint8_t> bytes(std::uint64_t at, std::size_t n) {
        std::vector<std::uint8_t> out(n);
        read(at, out.data(), n);
        return out;
    }

    // When and how large: what tells one archive from another.
    std::string key() const {
        std::error_code ec;
        std::uint64_t hash = 1469598103934665603ULL;
        auto mix = [&hash](std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                hash = (hash ^ ((v >> (8 * i)) & 0xFF)) * 1099511628211ULL;
            }
        };
        for (const auto& path : paths_) {
            for (const char c : std::filesystem::absolute(path, ec).generic_string()) {
                mix(static_cast<unsigned char>(c));
            }
            mix(static_cast<std::uint64_t>(std::filesystem::last_write_time(path, ec).time_since_epoch().count()));
        }
        mix(size_);
        char text[20];
        std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
        return text;
    }

private:
    void add(const std::filesystem::path& path) {
        std::error_code ec;
        const auto n = std::filesystem::file_size(path, ec);
        if (ec) {
            throw FormatError("cannot read " + path.filename().string());
        }
        paths_.push_back(path);
        starts_.push_back(size_);
        sizes_.push_back(n);
        size_ += n;
    }

    std::vector<std::filesystem::path> paths_;
    std::vector<std::uint64_t> starts_, sizes_;
    std::uint64_t size_ = 0;
    std::ifstream file_;
    std::size_t open_ = static_cast<std::size_t>(-1);
};

std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const std::uint8_t* p) { return u16(p) | (static_cast<std::uint32_t>(u16(p + 2)) << 16); }
std::uint64_t u64(const std::uint8_t* p) { return u32(p) | (static_cast<std::uint64_t>(u32(p + 4)) << 32); }

struct Entry {
    std::string name; // a safe relative path, or empty to leave it out
    std::uint16_t flags = 0, method = 0;
    std::uint32_t crc = 0;
    std::uint64_t packed = 0, size = 0, header = 0;
};

// The entry's name as a path below the cache folder: never one that climbs
// out of it, begins at a root or names a drive.
std::string safeName(const std::string& raw) {
    std::string out;
    std::string part;
    auto flush = [&]() -> bool {
        if (part == "..") {
            return false;
        }
        if (!part.empty() && part != ".") {
            if (!out.empty()) {
                out += '/';
            }
            out += part;
        }
        part.clear();
        return true;
    };
    for (const char c : raw) {
        if (c == '/' || c == '\\') {
            if (!flush()) {
                return "";
            }
        } else if (c == ':' || static_cast<unsigned char>(c) < 0x20) {
            return "";
        } else {
            part += c;
        }
    }
    return flush() ? out : "";
}

std::vector<Entry> readDirectory(Parts& zip) {
    // The end record: its signature, searched for from the end back over
    // the longest comment there can be.
    const auto size = zip.size();
    const auto tail = static_cast<std::size_t>(std::min<std::uint64_t>(size, 22 + 65535));
    const auto end = zip.bytes(size - tail, tail);
    std::size_t at = tail;
    bool found = false;
    while (at >= 22) {
        at -= 1;
        if (at + 22 <= tail && u32(&end[at]) == 0x06054b50) {
            found = true;
            break;
        }
    }
    if (!found) {
        throw FormatError("not a zip archive (or not its last part)");
    }
    std::uint64_t count = u16(&end[at + 10]);
    std::uint64_t dirSize = u32(&end[at + 12]);
    std::uint64_t dirAt = u32(&end[at + 16]);
    const auto endAt = size - tail + at;
    // The 64-bit end record, found through the 20-byte locator just before.
    if (endAt >= 20) {
        const auto locator = zip.bytes(endAt - 20, 20);
        if (u32(&locator[0]) == 0x07064b50) {
            const auto recordAt = u64(&locator[8]);
            const auto record = zip.bytes(recordAt, 56);
            if (u32(&record[0]) != 0x06064b50) {
                throw FormatError("damaged zip archive");
            }
            count = u64(&record[32]);
            dirSize = u64(&record[40]);
            dirAt = u64(&record[48]);
        }
    }
    if (count > kMaxEntries || dirSize > (64U << 20) || dirAt > size || dirSize > size - dirAt) {
        throw FormatError("damaged zip archive");
    }
    const auto dir = zip.bytes(dirAt, static_cast<std::size_t>(dirSize));
    std::vector<Entry> entries;
    std::size_t p = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (p + 46 > dir.size() || u32(&dir[p]) != 0x02014b50) {
            throw FormatError("damaged zip archive");
        }
        Entry e;
        e.flags = u16(&dir[p + 8]);
        e.method = u16(&dir[p + 10]);
        e.crc = u32(&dir[p + 16]);
        e.packed = u32(&dir[p + 20]);
        e.size = u32(&dir[p + 24]);
        const std::size_t nameLen = u16(&dir[p + 28]);
        const std::size_t extraLen = u16(&dir[p + 30]);
        const std::size_t commentLen = u16(&dir[p + 32]);
        e.header = u32(&dir[p + 42]);
        if (p + 46 + nameLen + extraLen + commentLen > dir.size()) {
            throw FormatError("damaged zip archive");
        }
        const std::string raw(reinterpret_cast<const char*>(&dir[p + 46]), nameLen);
        // The 64-bit sizes: in field 1 of the extra data, each one present
        // only where its 32-bit place holds all ones, in this order.
        for (std::size_t x = p + 46 + nameLen; x + 4 <= p + 46 + nameLen + extraLen;) {
            const auto id = u16(&dir[x]);
            const std::size_t len = u16(&dir[x + 2]);
            const auto fieldEnd = x + 4 + len;
            if (fieldEnd > p + 46 + nameLen + extraLen) {
                break;
            }
            if (id == 0x0001) {
                std::size_t q = x + 4;
                auto take = [&](std::uint64_t& value) {
                    if (value == 0xFFFFFFFFU && q + 8 <= fieldEnd) {
                        value = u64(&dir[q]);
                        q += 8;
                    }
                };
                take(e.size);
                take(e.packed);
                take(e.header);
            }
            x = fieldEnd;
        }
        if (!raw.empty() && raw.back() != '/' && raw.back() != '\\') {
            e.name = safeName(raw);
        }
        entries.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
}

void unpackEntry(Parts& zip, const Entry& e, const std::filesystem::path& to) {
    if ((e.flags & 1) != 0) {
        throw FormatError("the zip archive is locked with a password; unpack it first");
    }
    if (e.method != 0 && e.method != 8) {
        throw FormatError("the zip archive packs " + e.name + " in a way Helmsman cannot read; unpack it first");
    }
    const auto local = zip.bytes(e.header, 30);
    if (u32(&local[0]) != 0x04034b50) {
        throw FormatError("damaged zip archive");
    }
    const auto dataAt = e.header + 30 + u16(&local[26]) + u16(&local[28]);
    if (dataAt > zip.size() || e.packed > zip.size() - dataAt) {
        throw FormatError("damaged zip archive");
    }
    std::filesystem::create_directories(to.parent_path());
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw FormatError("cannot write " + to.string());
    }
    std::vector<std::uint8_t> in(1 << 20);
    std::vector<std::uint8_t> buf(1 << 20);
    mz_ulong crc = mz_crc32(0, nullptr, 0);
    std::uint64_t written = 0;
    auto put = [&](const std::uint8_t* data, std::size_t n) {
        if (n > e.size - written) {
            throw FormatError("damaged zip archive");
        }
        crc = mz_crc32(crc, data, n);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
        written += n;
    };
    std::uint64_t left = e.packed;
    std::uint64_t at = dataAt;
    if (e.method == 0) {
        while (left > 0) {
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(left, in.size()));
            zip.read(at, in.data(), n);
            put(in.data(), n);
            at += n;
            left -= n;
        }
    } else {
        mz_stream s{};
        if (mz_inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            throw FormatError("cannot unpack the zip archive");
        }
        int status = MZ_OK;
        while (status != MZ_STREAM_END) {
            if (s.avail_in == 0 && left > 0) {
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(left, in.size()));
                zip.read(at, in.data(), n);
                at += n;
                left -= n;
                s.next_in = in.data();
                s.avail_in = static_cast<unsigned>(n);
            }
            s.next_out = buf.data();
            s.avail_out = static_cast<unsigned>(buf.size());
            // No MZ_FINISH: it asks for the whole file in one call.
            status = mz_inflate(&s, MZ_NO_FLUSH);
            const auto made = buf.size() - s.avail_out;
            const bool stuck = status == MZ_BUF_ERROR && made == 0;
            if ((status != MZ_OK && status != MZ_STREAM_END && status != MZ_BUF_ERROR) || stuck) {
                mz_inflateEnd(&s);
                throw FormatError("damaged zip archive (" + e.name + ")");
            }
            try {
                put(buf.data(), made);
            } catch (...) {
                mz_inflateEnd(&s);
                throw;
            }
        }
        mz_inflateEnd(&s);
    }
    if (written != e.size || static_cast<std::uint32_t>(crc) != e.crc) {
        throw FormatError("damaged zip archive (" + e.name + ")");
    }
    if (!out.flush()) {
        throw FormatError("cannot write " + to.string() + " (is the disk full?)");
    }
}

} // namespace

bool isZipName(const std::filesystem::path& path) {
    const auto name = lower(path.filename().string());
    auto ends = [&name](const char* tail) {
        const std::string t(tail);
        return name.size() > t.size() && name.compare(name.size() - t.size(), t.size(), t) == 0;
    };
    if (ends(".zip") || ends(".zip.001")) {
        return true;
    }
    if (ends(".001")) {
        std::ifstream in(path, std::ios::binary);
        std::array<char, 4> magic{};
        in.read(magic.data(), 4);
        return in && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
    }
    return false;
}

std::filesystem::path unpackZip(const std::filesystem::path& path, const std::filesystem::path& cacheRoot) {
    if (cacheRoot.empty()) {
        throw FormatError("there is no folder to unpack the zip archive into");
    }
    Parts zip(path);
    const auto key = zip.key();
    const auto folder = cacheRoot / key;
    std::error_code ec;
    if (std::filesystem::is_regular_file(folder / ".unpacked", ec)) {
        return folder;
    }
    const auto entries = readDirectory(zip);
    std::uint64_t total = 0;
    for (const auto& e : entries) {
        total += e.size;
    }
    if (total > kMaxTotal) {
        throw FormatError("the zip archive is larger than a CD's files can be");
    }
    const auto space = std::filesystem::space(cacheRoot.parent_path().empty() ? cacheRoot : cacheRoot.parent_path(), ec);
    if (!ec && space.available < total + (64U << 20)) {
        throw FormatError("not enough free disk space to unpack the zip archive");
    }
    // Into a folder of its own first, so a half-done unpacking is never
    // taken for a whole one.
    const auto work = cacheRoot / (key + ".part");
    std::filesystem::remove_all(work, ec);
    try {
        for (const auto& e : entries) {
            if (!e.name.empty()) {
                unpackEntry(zip, e, work / std::filesystem::path(e.name));
            }
        }
        std::ofstream(work / ".unpacked") << path.filename().string() << '\n';
        std::filesystem::remove_all(folder, ec);
        std::filesystem::rename(work, folder);
    } catch (...) {
        std::filesystem::remove_all(work, ec);
        throw;
    }
    return folder;
}

} // namespace helmsman
