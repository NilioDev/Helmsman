// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second game's music and sound effects. Each part of an episode has a
// suite: a music file (.AD) and a table (.SUI) that says which sound each
// effect number plays. See docs/rules/sequel-sound.md.
#include "game_impl.hpp"

namespace helmsman::sequel::detail {

namespace {

constexpr int kNone = 0xFE;     // in a song request: no song
constexpr int kKeep = -1;       // in a song request: as it is
constexpr int kStopAll = 0xFE;  // in place of an effect: silence them all
constexpr int kRawRate = 11025; // the plain sounds the suites name
constexpr std::size_t kSlots = 8;
constexpr std::size_t kSlotName = 10;
constexpr std::size_t kTable = kSlots * kSlotName;
constexpr std::size_t kEffectSize = 12;

} // namespace

void Game::startMusic() {
    if (const auto library = assets_.data().load("TREK2.AD")) {
        try {
            adlib_.setInstruments(*library);
        } catch (const FormatError&) {
        }
    }
    previousWait_ = p_.onWait;
    p_.onWait = [this](Platform& pf) {
        if (previousWait_) {
            previousWait_(pf);
        }
        pumpMusic();
    };
}

void Game::endMusic() {
    p_.onWait = previousWait_;
    p_.stopSound(SoundChannel::Music);
}

// Reads a suite: its music and its table of effects. Nothing plays until
// the scripts ask for a song.
void Game::openSuite(const std::string& name) {
    if (name == suiteName_) {
        return;
    }
    // A song asked for before any suite was read waits for this one.
    resumeSong_ = resumeSong_ || suiteName_.empty();
    adlib_.stop();
    p_.stopSound(SoundChannel::Music);
    suiteName_ = name;
    suiteSlots_ = {};
    suiteEffects_.clear();
    songLoaded_ = false;
    if (const auto bytes = file(name + ".AD")) {
        try {
            adlib_.setSongs(audio::readXmidi(*bytes));
            songLoaded_ = true;
        } catch (const FormatError&) {
        }
    }
    if (const auto table = file(name + ".SUI"); table && table->size() >= kTable + 2) {
        const auto& t = *table;
        auto name0 = [&](std::size_t at, std::size_t max) {
            std::string s;
            for (std::size_t i = 0; i < max && t[at + i] != 0; ++i) {
                s.push_back(static_cast<char>(t[at + i]));
            }
            return s;
        };
        for (std::size_t i = 0; i < kSlots; ++i) {
            suiteSlots_[i] = name0(i * kSlotName, kSlotName);
        }
        const std::size_t count = static_cast<std::size_t>(t[kTable] | (t[kTable + 1] << 8));
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t at = kTable + 2 + i * kEffectSize;
            if (at + kEffectSize > t.size()) {
                break;
            }
            SuiteEffect e;
            e.name = name0(at, 9);
            e.slot = static_cast<std::int16_t>(t[at + 10] | (t[at + 11] << 8));
            suiteEffects_.push_back(std::move(e));
        }
    }
    sounding_ = false;
    const int want = wantSong_;
    wantSong_ = -1;
    if (resumeSong_) {
        resumeSong_ = false;
        startSong(want);
    }
}

// The bridge's suite ('s'), the away missions' ('g'), or one by its own
// name (kind 0). The first time an episode's music is heard, its opening
// piece (the "i" suite) plays first.
void Game::loadSuite(const std::string& base, char kind) {
    const std::string b = upperName(base);
    std::string name = kind != 0 ? b.substr(0, 7) + static_cast<char>(std::toupper(static_cast<unsigned char>(kind)))
                                 : b.substr(0, 8);
    if (b != suiteBase_) {
        suiteBase_ = b;
        const std::string intro = b.substr(0, 7) + "I";
        if (musicOn_ && file(intro + ".AD")) {
            // The song to go on with (of a saved game) waits for it.
            const bool resume = resumeSong_ || suiteName_.empty();
            const int want = wantSong_;
            resumeSong_ = false;
            afterIntro_ = name; // no song of the opening's own
            openSuite(intro);
            resumeSong_ = false;
            wantSong_ = resume ? want : -1;
            if (songLoaded_ && adlib_.songCount() > 0) {
                adlib_.play(0);
                sounding_ = true;
                if (p_.devTrace) {
                    std::fprintf(stderr, "music %s opening, then %s\n", intro.c_str(), name.c_str());
                }
                return;
            }
            afterIntro_.clear();
            resumeSong_ = resume;
        }
    }
    if (!afterIntro_.empty()) {
        // Still the opening piece: the suite comes after it.
        afterIntro_ = name;
        return;
    }
    openSuite(name);
}

void Game::startSong(int song) {
    wantSong_ = song;
    if (!afterIntro_.empty()) {
        return; // after the opening piece
    }
    adlib_.stop();
    p_.stopSound(SoundChannel::Music);
    sounding_ = false;
    if (song >= 0 && musicOn_ && songLoaded_ && song < adlib_.songCount()) {
        adlib_.play(song);
        sounding_ = true;
    }
    if (p_.devTrace) {
        std::fprintf(stderr, "music %s song %d (background %d)%s\n", suiteName_.c_str(), song, backgroundSong_,
                     sounding_ ? "" : " silent");
    }
}

// What the scripts ask for: a song to play now, and the one to go on with
// after it (the background). kNone is no song; kKeep leaves it as it is.
// A song asked for while another plays over the background waits for the
// background if it is that background.
void Game::music(int now, int background) {
    bool over = wantSong_ != backgroundSong_;
    if (background == now) {
        over = false;
    }
    if (background == kNone) {
        backgroundSong_ = -1;
    } else if (background != kKeep) {
        backgroundSong_ = background;
    }
    if (now == kKeep) {
        now = backgroundSong_;
    } else if (now == kNone) {
        now = -1;
    }
    if (wantSong_ == now || (over && backgroundSong_ == now)) {
        return;
    }
    startSong(now);
}

void Game::setMusicOn(bool on) {
    if (on == musicOn_) {
        return;
    }
    musicOn_ = on;
    if (!on) {
        adlib_.stop();
        p_.stopSound(SoundChannel::Music);
        wantSong_ = -1;
    } else {
        startSong(backgroundSong_);
    }
}

void Game::pumpMusic() {
    if (!musicOn_) {
        return;
    }
    if (!adlib_.playing()) {
        if (!sounding_) {
            return;
        }
        sounding_ = false;
        // A song ended: the opening piece gives way to the suite, the
        // background starts over, anything else gives way to it.
        if (!afterIntro_.empty()) {
            const std::string next = afterIntro_;
            afterIntro_.clear();
            resumeSong_ = true;
            openSuite(next);
        } else if (wantSong_ == backgroundSong_ && wantSong_ >= 0) {
            startSong(wantSong_);
        } else {
            startSong(backgroundSong_);
        }
        if (!adlib_.playing()) {
            return;
        }
    }
    constexpr std::size_t kFrames = 1024;
    auto& queued = p_.soundQueued[static_cast<int>(SoundChannel::Music)];
    int guard = 0;
    while (queued < 0.15 && guard++ < 8 && adlib_.playing()) {
        SoundCommand c;
        c.channel = SoundChannel::Music;
        c.rate = adlib_.rate();
        c.wide.resize(kFrames * 2);
        adlib_.render(c.wide.data(), kFrames);
        queued += static_cast<double>(kFrames) / adlib_.rate();
        p_.sound.push_back(std::move(c));
    }
}

// A sound by its name: from the sound banks, or a plain file of the
// archives.
std::optional<PlainSound> Game::findSound(const std::string& name) {
    const std::string tag = upperName(name);
    std::optional<PlainSound> sound;
    if (voices_ && voices_->has(tag)) {
        try {
            sound = voices_->sound(tag);
        } catch (const FormatError&) {
        }
    }
    if (!sound) {
        sound = assets_.sound(tag);
    }
    if (!sound) {
        if (auto raw = file(tag + ".RAW")) {
            sound = PlainSound{kRawRate, std::move(*raw)};
        }
    }
    return sound;
}

// Effect `number` of the suite, as loud as `volume` (0 to 127) and from
// `pan` (0 left, 64 the middle, 127 right).
void Game::soundEffect(int number, int volume, int pan) {
    if (!soundOn_) {
        return;
    }
    if (number == kStopAll) {
        p_.stopSound(SoundChannel::Effect);
        p_.stopSound(SoundChannel::Ambient);
        return;
    }
    if (number < 0 || number >= static_cast<int>(suiteEffects_.size())) {
        return;
    }
    const SuiteEffect& e = suiteEffects_[static_cast<std::size_t>(number)];
    std::string name = e.name;
    if (e.slot >= 0 && e.slot < static_cast<int>(kSlots)) {
        name = suiteSlots_[static_cast<std::size_t>(e.slot)];
    }
    if (name.empty()) {
        return;
    }
    auto sound = findSound(name);
    if (p_.devTrace) {
        std::fprintf(stderr, "effect %d: %s%s\n", number, name.c_str(), sound ? "" : " (missing)");
    }
    if (!sound || sound->rate <= 0) {
        return;
    }
    volume = std::clamp(volume, 0, 127);
    pan = std::clamp(pan, 0, 127);
    SoundCommand c;
    c.channel = SoundChannel::Effect;
    c.rate = sound->rate;
    c.wide.resize(sound->samples.size() * 2);
    const int left = volume * std::min(127, (127 - pan) * 2) / 127;
    const int right = volume * std::min(127, pan * 2) / 127;
    for (std::size_t i = 0; i < sound->samples.size(); ++i) {
        const int v = (static_cast<int>(sound->samples[i]) - 128) * 256;
        c.wide[2 * i] = static_cast<std::int16_t>(v * left / 127);
        c.wide[2 * i + 1] = static_cast<std::int16_t>(v * right / 127);
    }
    p_.soundQueued[static_cast<int>(SoundChannel::Effect)] +=
        static_cast<double>(sound->samples.size()) / sound->rate;
    p_.sound.push_back(std::move(c));
}

void Game::saveMusic(SaveWriter& w) const {
    w.s32(wantSong_);
    w.s32(backgroundSong_);
}

void Game::restoreMusic(SaveReader& r) {
    wantSong_ = r.s32();
    backgroundSong_ = r.s32();
    // The suite is read again where the game goes on, and the song with it.
    suiteName_.clear();
    resumeSong_ = true;
    adlib_.stop();
    p_.stopSound(SoundChannel::Music);
}

} // namespace helmsman::sequel::detail
