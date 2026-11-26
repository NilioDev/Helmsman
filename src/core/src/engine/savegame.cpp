// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/engine/savegame.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <chrono>
#include <system_error>

namespace helmsman {

namespace {

constexpr char kMagic[8] = {'H', 'L', 'M', 'S', 'A', 'V', 'E', 0};
constexpr std::uint32_t kVersion = 1;
constexpr std::uintmax_t kMaxFile = 4u << 20;

std::filesystem::path fileOf(const std::filesystem::path& folder, std::string_view game, int number) {
    return folder / (std::string(game) + "-" + std::to_string(number) + ".hsav");
}

struct Header {
    std::string description;
    std::uint64_t time = 0;
    std::size_t payloadAt = 0;
};

std::optional<std::vector<std::uint8_t>> load(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxFile) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<Header> readHeader(std::span<const std::uint8_t> bytes, std::string_view game) {
    try {
        SaveReader r(bytes);
        const auto magic = r.bytes(8);
        if (std::memcmp(magic.data(), kMagic, 8) != 0 || r.u32() != kVersion || r.text() != game) {
            return std::nullopt;
        }
        Header h;
        h.description = r.text();
        h.time = r.u32();
        h.time |= static_cast<std::uint64_t>(r.u32()) << 32;
        h.payloadAt = r.pos();
        return h;
    } catch (const FormatError&) {
        return std::nullopt;
    }
}

} // namespace

std::vector<SaveSlot> listSaves(const std::filesystem::path& folder, std::string_view game) {
    std::vector<std::pair<std::uint64_t, SaveSlot>> found;
    for (int n = 0; n < kMaxSaves; ++n) {
        const auto bytes = load(fileOf(folder, game, n));
        if (!bytes) {
            continue;
        }
        if (const auto h = readHeader(*bytes, game)) {
            found.push_back({h->time, SaveSlot{n, h->description}});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<SaveSlot> out;
    for (auto& f : found) {
        out.push_back(std::move(f.second));
    }
    return out;
}

bool writeSave(const std::filesystem::path& folder, std::string_view game, int number, std::string_view description,
               std::span<const std::uint8_t> payload) {
    if (number < 0 || number >= kMaxSaves) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    SaveWriter w;
    w.data.insert(w.data.end(), kMagic, kMagic + 8);
    w.u32(kVersion);
    w.text(game);
    w.text(description);
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count());
    w.u32(static_cast<std::uint32_t>(now));
    w.u32(static_cast<std::uint32_t>(now >> 32));
    w.data.insert(w.data.end(), payload.begin(), payload.end());
    // Written beside it first, so a failed write never spoils an older save.
    const auto path = fileOf(folder, game, number);
    auto temp = path;
    temp += ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<const char*>(w.data.data()), static_cast<std::streamsize>(w.data.size()))) {
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    return !ec;
}

std::optional<std::vector<std::uint8_t>> readSave(const std::filesystem::path& folder, std::string_view game, int number) {
    if (number < 0 || number >= kMaxSaves) {
        return std::nullopt;
    }
    auto bytes = load(fileOf(folder, game, number));
    if (!bytes) {
        return std::nullopt;
    }
    const auto h = readHeader(*bytes, game);
    if (!h) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(bytes->begin() + static_cast<std::ptrdiff_t>(h->payloadAt), bytes->end());
}

int freeSaveNumber(const std::filesystem::path& folder, std::string_view game) {
    for (int n = 0; n < kMaxSaves; ++n) {
        std::error_code ec;
        if (!std::filesystem::exists(fileOf(folder, game, n), ec)) {
            return n;
        }
    }
    return -1;
}

} // namespace helmsman
