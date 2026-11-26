// SPDX-License-Identifier: GPL-3.0-or-later
//
// CD music kept as music files beside a cue sheet (.wav, .ogg, .flac,
// .mp3), turned into the CD's own samples: 16-bit stereo at 44100 Hz.
#include "disc_impl.hpp"

#include <dr_libs/dr_flac.h>
#include <dr_libs/dr_mp3.h>
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis/stb_vorbis.c>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace helmsman {

namespace {

constexpr std::uint32_t kRate = 44100;
constexpr std::uint64_t kMaxFile = 1ull << 30;
constexpr std::uint64_t kMaxFrames = 74ull * 60 * kRate; // a whole CD

enum class Kind { Wave, Ogg, Flac, Mp3 };

std::vector<std::uint8_t> wholeFile(const std::filesystem::path& path) {
    const auto size = fileSizeOf(path);
    if (size > kMaxFile) {
        throw FormatError(path.filename().string() + " is too large for a music track");
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in) {
        throw FormatError("cannot read " + path.filename().string());
    }
    return out;
}

Kind kindOf(const std::vector<std::uint8_t>& b) {
    if (b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "WAVE", 4) == 0) {
        return Kind::Wave;
    }
    if (b.size() >= 4 && std::memcmp(b.data(), "OggS", 4) == 0) {
        return Kind::Ogg;
    }
    if (b.size() >= 4 && std::memcmp(b.data(), "fLaC", 4) == 0) {
        return Kind::Flac;
    }
    return Kind::Mp3;
}

// Samples of any rate and number of channels, as the CD's.
struct Pcm {
    std::vector<std::int16_t> samples; // interleaved
    unsigned channels = 2;
    unsigned rate = kRate;
};

std::uint64_t cdFrames(std::uint64_t frames, unsigned rate) { return rate == kRate ? frames : frames * kRate / rate; }

std::vector<std::int16_t> toCd(const Pcm& p) {
    const std::uint64_t in = p.channels == 0 ? 0 : p.samples.size() / p.channels;
    const std::uint64_t out = cdFrames(in, p.rate);
    std::vector<std::int16_t> o(static_cast<std::size_t>(out * 2));
    auto at = [&](std::uint64_t f, unsigned c) -> int {
        f = std::min<std::uint64_t>(f, in - 1);
        return p.samples[static_cast<std::size_t>(f * p.channels + std::min(c, p.channels - 1))];
    };
    for (std::uint64_t i = 0; i < out; ++i) {
        // Straight between the two nearest samples.
        const std::uint64_t num = i * p.rate;
        const std::uint64_t f = num / kRate;
        const auto frac = static_cast<int>(num % kRate);
        for (unsigned c = 0; c < 2; ++c) {
            const int a = at(f, c);
            const int b = at(f + 1, c);
            o[static_cast<std::size_t>(i * 2 + c)] =
                static_cast<std::int16_t>(a + static_cast<int>((static_cast<long long>(b - a) * frac) / kRate));
        }
    }
    return o;
}

struct WaveInfo {
    std::uint64_t dataAt = 0;
    std::uint64_t dataSize = 0;
    unsigned channels = 0;
    unsigned rate = 0;
    unsigned bits = 0;
};

WaveInfo readWave(const std::vector<std::uint8_t>& b) {
    WaveInfo w;
    Reader r(b);
    r.seek(12);
    bool haveFormat = false;
    while (r.remaining() >= 8) {
        const auto id = r.bytes(4);
        const std::uint32_t size = r.u32();
        const std::size_t at = b.size() - r.remaining();
        if (std::memcmp(id.data(), "fmt ", 4) == 0 && size >= 16) {
            const std::uint16_t format = r.u16();
            w.channels = r.u16();
            w.rate = r.u32();
            r.skip(6);
            w.bits = r.u16();
            haveFormat = format == 1 || format == 0xFFFE;
        } else if (std::memcmp(id.data(), "data", 4) == 0) {
            w.dataAt = at;
            w.dataSize = std::min<std::uint64_t>(size, b.size() - at);
            break;
        }
        const std::size_t next = at + size + (size & 1);
        if (next > b.size()) {
            break;
        }
        r.seek(next);
    }
    if (!haveFormat || w.bits != 16 || w.channels == 0 || w.channels > 8 || w.rate < 8000 || w.rate > 192000 ||
        w.dataAt == 0) {
        throw FormatError("a music track's .wav is not 16-bit PCM");
    }
    return w;
}

class MusicStore;
// The music files decoded last; older ones let their samples go.
std::vector<MusicStore*>& decoded() {
    static std::vector<MusicStore*> list;
    return list;
}

class MusicStore final : public Disc::Store {
public:
    explicit MusicStore(std::filesystem::path path) : path_(std::move(path)) {
        auto bytes = wholeFile(path_);
        kind_ = kindOf(bytes);
        std::uint64_t frames = 0;
        unsigned rate = kRate;
        switch (kind_) {
        case Kind::Wave: {
            const auto w = readWave(bytes);
            frames = w.dataSize / (2u * w.channels);
            rate = w.rate;
            break;
        }
        case Kind::Ogg: {
            int error = 0;
            stb_vorbis* v = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
            if (v == nullptr) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            const auto info = stb_vorbis_get_info(v);
            frames = stb_vorbis_stream_length_in_samples(v);
            rate = info.sample_rate;
            stb_vorbis_close(v);
            break;
        }
        case Kind::Flac: {
            drflac* f = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
            if (f == nullptr) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            frames = f->totalPCMFrameCount;
            rate = f->sampleRate;
            drflac_close(f);
            break;
        }
        case Kind::Mp3: {
            drmp3 m;
            if (!drmp3_init_memory(&m, bytes.data(), bytes.size(), nullptr)) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            frames = drmp3_get_pcm_frame_count(&m);
            rate = m.sampleRate;
            drmp3_uninit(&m);
            break;
        }
        }
        if (rate < 8000 || rate > 192000) {
            throw FormatError("the music track " + path_.filename().string() + " has an odd sample rate");
        }
        frames_ = std::min(cdFrames(frames, rate), kMaxFrames);
    }
    ~MusicStore() override {
        auto& list = decoded();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
    MusicStore(const MusicStore&) = delete;
    MusicStore& operator=(const MusicStore&) = delete;

    std::uint64_t size() const override { return frames_ * 4; }
    void read(std::uint64_t at, std::uint8_t* out, std::size_t n) override {
        if (at > size() || n > size() - at) {
            throw FormatError("the music track ends too soon");
        }
        load();
        // Where decoding gave fewer samples than the file said: silence.
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t sample = (at + i) / 2;
            const std::int16_t v = sample < samples_.size() ? samples_[static_cast<std::size_t>(sample)] : std::int16_t{0};
            out[i] = static_cast<std::uint8_t>(((at + i) & 1) == 0 ? (v & 0xFF) : ((v >> 8) & 0xFF));
        }
    }
    void unload() {
        samples_.clear();
        samples_.shrink_to_fit();
    }

private:
    void load() {
        auto& list = decoded();
        if (!samples_.empty()) {
            return;
        }
        const auto bytes = wholeFile(path_);
        Pcm p;
        switch (kind_) {
        case Kind::Wave: {
            const auto w = readWave(bytes);
            p.channels = w.channels;
            p.rate = w.rate;
            p.samples.resize(static_cast<std::size_t>(w.dataSize / 2));
            for (std::size_t i = 0; i < p.samples.size(); ++i) {
                const std::size_t a = static_cast<std::size_t>(w.dataAt) + 2 * i;
                p.samples[i] = static_cast<std::int16_t>(bytes[a] | (bytes[a + 1] << 8));
            }
            break;
        }
        case Kind::Ogg: {
            int error = 0;
            stb_vorbis* v = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
            if (v == nullptr) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            const auto info = stb_vorbis_get_info(v);
            p.channels = static_cast<unsigned>(std::max(info.channels, 1));
            p.rate = info.sample_rate;
            std::vector<short> chunk(4096 * p.channels);
            for (;;) {
                const int got = stb_vorbis_get_samples_short_interleaved(v, static_cast<int>(p.channels), chunk.data(),
                                                                         static_cast<int>(chunk.size()));
                if (got <= 0 || p.samples.size() / p.channels > kMaxFrames) {
                    break;
                }
                p.samples.insert(p.samples.end(), chunk.begin(), chunk.begin() + got * static_cast<int>(p.channels));
            }
            stb_vorbis_close(v);
            break;
        }
        case Kind::Flac: {
            drflac* f = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
            if (f == nullptr) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            p.channels = std::max<unsigned>(f->channels, 1);
            p.rate = f->sampleRate;
            const auto frames = std::min<std::uint64_t>(f->totalPCMFrameCount, kMaxFrames * 5);
            p.samples.resize(static_cast<std::size_t>(frames * p.channels));
            const auto got = drflac_read_pcm_frames_s16(f, frames, p.samples.data());
            p.samples.resize(static_cast<std::size_t>(got * p.channels));
            drflac_close(f);
            break;
        }
        case Kind::Mp3: {
            drmp3 m;
            if (!drmp3_init_memory(&m, bytes.data(), bytes.size(), nullptr)) {
                throw FormatError("cannot read the music track " + path_.filename().string());
            }
            p.channels = std::max<unsigned>(m.channels, 1);
            p.rate = m.sampleRate;
            std::vector<drmp3_int16> chunk(4096 * p.channels);
            for (;;) {
                const auto got = drmp3_read_pcm_frames_s16(&m, 4096, chunk.data());
                if (got == 0 || p.samples.size() / p.channels > kMaxFrames * 5) {
                    break;
                }
                p.samples.insert(p.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * p.channels));
            }
            drmp3_uninit(&m);
            break;
        }
        }
        if (p.rate == 0 || p.samples.empty()) {
            p.samples.assign(2, 0);
            p.channels = 2;
            p.rate = kRate;
        }
        samples_ = (p.rate == kRate && p.channels == 2) ? std::move(p.samples) : toCd(p);
        list.push_back(this);
        // Two tracks at most stay decoded: the one playing and the next.
        while (list.size() > 2) {
            MusicStore* old = list.front();
            list.erase(list.begin());
            old->unload();
        }
    }

    std::filesystem::path path_;
    Kind kind_ = Kind::Wave;
    std::uint64_t frames_ = 0;
    std::vector<std::int16_t> samples_;
};

} // namespace

bool isMusicFile(const std::filesystem::path& path, const std::string& cueType) {
    if (cueType == "WAVE" || cueType == "MP3" || cueType == "OGG" || cueType == "FLAC" || cueType == "VORBIS") {
        return true;
    }
    const auto ext = lowerText(path.extension().string());
    return ext == ".wav" || ext == ".ogg" || ext == ".flac" || ext == ".mp3";
}

std::unique_ptr<Disc::Store> musicStore(const std::filesystem::path& path) { return std::make_unique<MusicStore>(path); }

} // namespace helmsman
