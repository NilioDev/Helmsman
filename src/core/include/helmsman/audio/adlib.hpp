// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays the games' music the way an AdLib card did: the songs of a music
// file (see xmidi.hpp) on nine FM voices, with the instruments from the
// games' instrument library. The FM chip itself is emulated by Nuked OPL3.
// See docs/formats/music.md.
#pragma once

#include "helmsman/audio/xmidi.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

struct _opl3_chip;

namespace helmsman::audio {

class AdLibMusic {
public:
    explicit AdLibMusic(int rate = 44100);
    ~AdLibMusic();
    AdLibMusic(const AdLibMusic&) = delete;
    AdLibMusic& operator=(const AdLibMusic&) = delete;

    int rate() const { return rate_; }

    // The instrument library (GTL.AD): (patch, bank, offset) entries, then
    // each instrument's FM settings.
    void setInstruments(std::span<const std::uint8_t> library);
    void setSongs(std::vector<XmidiSong> songs);
    int songCount() const { return static_cast<int>(songs_.size()); }

    void play(int song);
    void stop();
    bool playing() const { return song_ >= 0; }
    int song() const { return song_; }

    // 0 (silent) to 127.
    void setVolume(int volume) { master_ = volume < 0 ? 0 : (volume > 127 ? 127 : volume); }

    // Fills `frames` frames of signed 16-bit stereo, left first.
    void render(std::int16_t* out, std::size_t frames);

private:
    struct Instrument {
        std::int8_t transpose = 0;
        std::array<std::uint8_t, 11> regs{};
    };
    struct Channel {
        int program = 0;
        int bank = 0;
        int volume = 127;
        int expression = 127;
        int bend = 0x2000;
        bool sustain = false;
    };
    struct Voice {
        bool on = false;
        bool held = false; // released while the sustain pedal is down
        int channel = -1;
        int note = 0;      // the song's note
        int played = 0;    // the note sounding
        int velocity = 0;
        std::uint32_t age = 0;
        const Instrument* instrument = nullptr;
    };
    struct Playing {
        int channel;
        int note;
        std::uint32_t ticksLeft;
    };
    struct Loop {
        std::size_t start;
        int count;
    };

    void write(int reg, int value);
    void tick();
    void runEvents();
    std::uint32_t readLength(std::size_t& p) const;
    void controller(int ch, int number, int value);
    void noteOn(int ch, int note, int velocity);
    void noteOff(int ch, int note);
    void releaseVoice(Voice& v);
    void setPitch(int index);
    void setLevel(int index);
    void silence();
    const Instrument* instrumentFor(int ch, int note, int& played) const;

    int rate_;
    std::unique_ptr<_opl3_chip> chip_;
    std::map<int, Instrument> instruments_; // bank << 8 | patch
    std::vector<XmidiSong> songs_;
    int song_ = -1;
    std::size_t pos_ = 0;
    std::uint32_t wait_ = 0;
    std::vector<Playing> notes_;
    std::vector<Loop> loops_;
    std::array<Channel, 16> channels_{};
    std::array<Voice, 9> voices_{};
    std::uint32_t clock_ = 0;
    double untilTick_ = 0;
    int master_ = 127;
};

} // namespace helmsman::audio
