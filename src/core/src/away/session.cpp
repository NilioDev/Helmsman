// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/away/session.hpp"

#include "helmsman/data/reader.hpp"
#include "helmsman/data/voc.hpp"

#include <array>

namespace helmsman::away {

namespace {

// Names of the item pictures, in item order (0x40 on).
constexpr std::array<ItemInfo, kItemCount> kItems{{
    {"iphasers", 0, true},  {"iphaserk", 1, true},  {"ihand", 2, false},     {"irock", 3, false},
    {"istricor", 4, true},  {"imtricor", 5, true},  {"ideadguy", 6, false},  {"icomm", 7, true},
    {"ipbc", 8, false},     {"irlg", 9, false},     {"iwrench", 10, false},  {"iinsulat", 11, false},
    {"isample", 12, false}, {"icure", 13, false},   {"idishes", 14, false},  {"irt", 15, false},
    {"irtwb", 16, false},   {"icombbit", 17, false}, {"ijnkmetl", 18, false}, {"iwiring", 19, false},
    {"iwirscrp", 20, false}, {"ipwf", 21, false},   {"ipwe", 22, false},     {"ideadph", 23, false},
    {"ibomb", 24, false},   {"imetal", 25, false},  {"iskull", 26, false},   {"imineral", 27, false},
    {"imeteor", 28, false}, {"ishells", 29, false}, {"idegrime", 30, false}, {"ilenses", 31, false},
    {"idisks", 32, false},  {"iantigra", 33, false}, {"in2gas", 34, false},  {"io2gas", 35, false},
    {"ih2gas", 36, false},  {"in2o", 37, false},    {"inh3", 38, false},     {"ih2o", 39, false},
    {"iwrod", 40, false},   {"iirod", 41, false},   {"iredgem", 42, false},  {"iredgem", 43, false},
    {"iredgem", 44, false}, {"igrngem", 45, false}, {"igrngem", 46, false},  {"igrngem", 47, false},
    {"iblugem", 48, false}, {"iblugem", 49, false}, {"iblugem", 50, false},  {"iconect", 51, false},
    {"is8rocks", 52, false}, {"iidcard", 53, false}, {"isnake", 54, false},  {"ifern", 55, false},
    {"icrystal", 56, false}, {"iknife", 57, false}, {"idetoxin", 58, false}, {"iberry", 59, false},
    {"idoover", 60, false}, {"ialiendv", 61, false}, {"icapsule", 62, false}, {"imedkit", 63, true},
    {"ibeam", 64, false},   {"idrill", 65, false},  {"ihypo", 66, false},    {"ifusion", 67, false},
    {"icable1", 68, false}, {"icable2", 69, false}, {"ilmd", 70, false},     {"ideck", 71, false},
    {"itech", 72, false},
}};

constexpr double kTicksPerSecond = 1193182.0 / 65536.0;

std::vector<std::uint8_t> joinParts(const VocSound& sound, int& rate) {
    std::vector<std::uint8_t> all;
    rate = 0;
    for (const auto& part : sound.parts) {
        if (rate == 0) {
            rate = part.rate;
        }
        all.insert(all.end(), part.samples.begin(), part.samples.end());
    }
    return all;
}

} // namespace

const ItemInfo& itemInfo(int item) {
    const int i = item - kFirstItem;
    return kItems[static_cast<std::size_t>(i >= 0 && i < kItemCount ? i : 0)];
}

Session::Session(Platform& p, GameSource& s)
    : platform(p), source(s), archive(Archive::open(s, "DATA")), assets(archive),
      font(Font::fromBytes(archive.need("FONT.FNT"))) {
    for (int i = 0; i < kItemCount; ++i) {
        carried[static_cast<std::size_t>(i)] = kItems[static_cast<std::size_t>(i)].startsWith;
    }
    startMillis_ = platform.millis;
    if (const auto library = archive.load("GTL.AD")) {
        music.setInstruments(*library);
    }
    previousWait_ = platform.onWait;
    platform.onWait = [this](Platform& pf) {
        if (previousWait_) {
            previousWait_(pf);
        }
        pumpMusic();
    };
}

Session::~Session() {
    platform.onWait = previousWait_;
    platform.stopSound(SoundChannel::Music);
}

void Session::loadMusic(std::string_view name) {
    const std::string file = upper(std::string(name)) + ".AD";
    if (musicLoaded_ && file == musicName_) {
        return;
    }
    stopMusic();
    musicName_ = file;
    musicLoaded_ = false;
    if (const auto bytes = archive.load(file)) {
        try {
            music.setSongs(audio::readXmidi(*bytes));
            musicLoaded_ = true;
        } catch (const FormatError&) {
        }
    }
}

void Session::playMusic(int song) {
    song_ = song;
    if (!musicLoaded_ || !musicOn_) {
        return;
    }
    platform.stopSound(SoundChannel::Music);
    music.play(song);
}

void Session::stopMusic() {
    song_ = -1;
    music.stop();
    platform.stopSound(SoundChannel::Music);
}

void Session::setMusicOn(bool on) {
    if (on == musicOn_) {
        return;
    }
    musicOn_ = on;
    if (!on) {
        music.stop();
        platform.stopSound(SoundChannel::Music);
    } else if (song_ >= 0) {
        playMusic(song_);
    }
}

void Session::save(SaveWriter& w) const {
    w.u8(kItemCount);
    for (const bool c : carried) {
        w.u8(c ? 1 : 0);
    }
    w.text(musicName_);
    w.s32(song_);
}

void Session::restore(SaveReader& r) {
    const int count = r.u8();
    for (int i = 0; i < count; ++i) {
        const bool c = r.u8() != 0;
        if (i < kItemCount) {
            carried[static_cast<std::size_t>(i)] = c;
        }
    }
    std::string name = r.text();
    const int song = r.s32();
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".AD") == 0) {
        name.resize(name.size() - 3);
        loadMusic(name);
        if (song >= 0) {
            playMusic(song);
        } else {
            stopMusic();
        }
    }
}

void Session::pumpMusic() {
    if (!music.playing() || !musicOn_) {
        return;
    }
    constexpr std::size_t kFrames = 1024;
    auto& queued = platform.soundQueued[static_cast<int>(SoundChannel::Music)];
    int guard = 0;
    while (queued < 0.15 && guard++ < 8) {
        SoundCommand c;
        c.channel = SoundChannel::Music;
        c.rate = music.rate();
        c.wide.resize(kFrames * 2);
        music.render(c.wide.data(), kFrames);
        queued += static_cast<double>(kFrames) / music.rate();
        platform.sound.push_back(std::move(c));
    }
}

std::uint64_t Session::ticks() const {
    return static_cast<std::uint64_t>(static_cast<double>(platform.millis - startMillis_) * kTicksPerSecond / 1000.0);
}

void Session::waitTick() {
    const std::uint64_t now = ticks();
    if (lastTick_ < now) {
        // Running behind (a long load): do not hurry to catch up.
        lastTick_ = now;
    }
    while (ticks() <= lastTick_) {
        platform.waitFrame();
    }
    ++lastTick_;
}

void Session::playSpeech(std::string_view id) {
    std::string path = "VOC/";
    for (const char c : id) {
        path.push_back(c == '\\' ? '/' : c);
    }
    path += ".VOC";
    stopSpeech();
    try {
        const auto bytes = source.read(path);
        if (!bytes) {
            return;
        }
        int rate = 0;
        auto samples = joinParts(Voc::parse(*bytes), rate);
        platform.queueSound(SoundChannel::Voice, rate, std::move(samples), true);
    } catch (const FormatError&) {
        // A damaged voice is left out.
    }
}

bool Session::speaking() const { return platform.soundQueued[static_cast<int>(SoundChannel::Voice)] > 0.02; }

void Session::stopSpeech() { platform.stopSound(SoundChannel::Voice); }

void Session::playEffect(std::string_view name, bool loop) {
    if (!effectsOn) {
        return;
    }
    std::string path = "VOC/SFX/";
    for (const char c : name) {
        path.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    path += ".VOC";
    try {
        auto bytes = source.read(path);
        if (!bytes) {
            // A few sounds are kept in the game's archive instead.
            bytes = archive.load(path.substr(8));
        }
        if (!bytes) {
            return;
        }
        int rate = 0;
        auto samples = joinParts(Voc::parse(*bytes), rate);
        if (loop) {
            platform.stopSound(SoundChannel::Ambient);
            // A minute of it: the room's sound goes on while the player is there.
            std::vector<std::uint8_t> repeated;
            while (!samples.empty() && repeated.size() < static_cast<std::size_t>(rate) * 60) {
                repeated.insert(repeated.end(), samples.begin(), samples.end());
            }
            platform.queueSound(SoundChannel::Ambient, rate, std::move(repeated));
        } else {
            platform.queueSound(SoundChannel::Effect, rate, std::move(samples), true);
        }
    } catch (const FormatError&) {
    }
}

void Session::stopEffects() {
    platform.stopSound(SoundChannel::Effect);
    platform.stopSound(SoundChannel::Ambient);
}

std::string Session::textLine(std::string_view file, int index) {
    const auto bytes = archive.load(upper(file) + ".TXT");
    if (!bytes) {
        return {};
    }
    int n = 0;
    std::string line;
    for (const auto b : *bytes) {
        if (b == 0) {
            if (n == index) {
                return line;
            }
            ++n;
            line.clear();
            continue;
        }
        line.push_back(static_cast<char>(b));
    }
    return n == index ? line : std::string{};
}

SpokenText splitSpoken(std::string_view text) {
    SpokenText out;
    if (!text.empty() && text[0] == '#') {
        const auto end = text.find('#', 1);
        if (end != std::string_view::npos) {
            out.voice = std::string(text.substr(1, end - 1));
            out.words = std::string(text.substr(end + 1));
            return out;
        }
    }
    out.words = std::string(text);
    return out;
}

} // namespace helmsman::away
