// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/data/game_source.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

namespace helmsman {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

constexpr GameId kGames[] = {GameId::Anniversary, GameId::JudgmentRites};

// The game's folder in an image: the folder of its program. The 1992 game
// keeps it in TREKCD, the sequel on the top of the CD.
class DiscSource final : public GameSource {
public:
    DiscSource(std::unique_ptr<Disc> disc, std::string root) : disc_(std::move(disc)), root_(std::move(root)) {}

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) override {
        const auto* file = disc_->find(root_ + normalPath(path));
        if (file == nullptr) {
            return std::nullopt;
        }
        return disc_->read(*file);
    }
    bool exists(std::string_view path) const override { return disc_->find(root_ + normalPath(path)) != nullptr; }
    std::vector<std::uint8_t> readPart(std::string_view path, std::uint64_t offset, std::size_t size) override {
        const auto* file = disc_->find(root_ + normalPath(path));
        if (file == nullptr) {
            throw FormatError("missing " + std::string(path));
        }
        return disc_->read(*file, offset, size);
    }
    std::uint64_t sizeOf(std::string_view path) const override {
        const auto* file = disc_->find(root_ + normalPath(path));
        return file == nullptr ? 0 : file->size;
    }
    std::vector<AudioTrack> audioTracks() const override { return disc_->audioTracks(); }
    std::vector<std::int16_t> readAudio(const AudioTrack& track, std::uint64_t frame, std::size_t count) override {
        return disc_->readAudio(track, frame, count);
    }

private:
    std::unique_ptr<Disc> disc_;
    std::string root_;
};

class FolderSource final : public GameSource {
public:
    explicit FolderSource(std::filesystem::path root) : root_(std::move(root)) {
        // Every file below the folder, by its normal path: the folder may
        // come from a CD or a case-sensitive file system.
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(root_, ec);
        int count = 0;
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 4 || ++count > 50000) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec)) {
                const auto rel = std::filesystem::relative(it->path(), root_, ec).generic_string();
                files_.emplace(normalPath(rel), it->path());
            }
        }
    }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) override {
        const auto* file = find(path);
        if (file == nullptr) {
            return std::nullopt;
        }
        std::ifstream in(*file, std::ios::binary);
        if (!in) {
            throw FormatError("cannot open " + file->filename().string());
        }
        std::vector<std::uint8_t> out((std::istreambuf_iterator<char>(in)), {});
        return out;
    }
    bool exists(std::string_view path) const override { return find(path) != nullptr; }
    std::vector<std::uint8_t> readPart(std::string_view path, std::uint64_t offset, std::size_t size) override {
        const auto* file = find(path);
        if (file == nullptr) {
            throw FormatError("missing " + std::string(path));
        }
        std::ifstream in(*file, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(offset));
        std::vector<std::uint8_t> out(size);
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        out.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
        return out;
    }
    std::uint64_t sizeOf(std::string_view path) const override {
        const auto* file = find(path);
        std::error_code ec;
        return file == nullptr ? 0 : std::filesystem::file_size(*file, ec);
    }

    const std::filesystem::path* find(std::string_view path) const {
        const auto it = files_.find(normalPath(path));
        return it == files_.end() ? nullptr : &it->second;
    }
    std::filesystem::path root_;
    std::map<std::string, std::filesystem::path> files_;
};

// The folder of `name` among normal paths, "" for the top, or nothing.
template <typename Paths>
std::optional<std::string> folderOf(const Paths& paths, std::string_view name) {
    std::optional<std::string> best;
    for (const auto& path : paths) {
        const auto slash = path.rfind('/');
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        if (file != name) {
            continue;
        }
        std::string folder = slash == std::string::npos ? "" : path.substr(0, slash + 1);
        // The program nearest the top is the game's.
        if (!best || folder.size() < best->size()) {
            best = folder;
        }
    }
    return best;
}

} // namespace

std::string_view programName(GameId game) {
    switch (game) {
    case GameId::Anniversary: return "STARTREK.EXE";
    case GameId::JudgmentRites: return "TREKJR.EXE";
    }
    return "";
}

std::unique_ptr<GameSource> GameSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        FolderSource probe(path);
        std::vector<std::string> paths;
        for (const auto& [name, file] : probe.files_) {
            paths.push_back(name);
        }
        for (const auto game : kGames) {
            const auto folder = folderOf(paths, programName(game));
            if (folder && probe.exists(*folder + "DATA.DIR")) {
                auto source = std::make_unique<FolderSource>(path / *folder);
                source->game_ = game;
                source->origin_ = path;
                return source;
            }
        }
        return nullptr;
    }
    const auto ext = lower(path.extension().string());
    if (ext != ".cue" && ext != ".bin" && ext != ".iso" && ext != ".img") {
        return nullptr;
    }
    auto disc = Disc::open(path);
    std::vector<std::string> paths;
    for (const auto& file : disc->files()) {
        paths.push_back(file.path);
    }
    for (const auto game : kGames) {
        const auto folder = folderOf(paths, programName(game));
        if (folder && disc->find(*folder + "DATA.DIR") != nullptr) {
            auto source = std::make_unique<DiscSource>(std::move(disc), *folder);
            source->game_ = game;
            source->origin_ = path;
            return source;
        }
    }
    return nullptr;
}

} // namespace helmsman
