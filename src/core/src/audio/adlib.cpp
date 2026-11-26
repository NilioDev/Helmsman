// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/audio/adlib.hpp"

extern "C" {
#include "opl3.h"
}

#include <algorithm>
#include <cmath>

namespace helmsman::audio {

namespace {

constexpr int kTicksPerSecond = 120;
constexpr int kPercussionChannel = 9;
constexpr int kPercussionBank = 127;
constexpr int kOperator[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

} // namespace

AdLibMusic::AdLibMusic(int rate) : rate_(rate > 0 ? rate : 44100), chip_(std::make_unique<opl3_chip>()) {
    OPL3_Reset(chip_.get(), static_cast<std::uint32_t>(rate_));
    write(0x01, 0x20); // let the instruments choose their wave forms
    write(0xBD, 0x00);
}

AdLibMusic::~AdLibMusic() = default;

void AdLibMusic::write(int reg, int value) {
    OPL3_WriteReg(chip_.get(), static_cast<std::uint16_t>(reg), static_cast<std::uint8_t>(value));
}

void AdLibMusic::setInstruments(std::span<const std::uint8_t> library) {
    instruments_.clear();
    for (std::size_t p = 0; p + 6 <= library.size(); p += 6) {
        const int patch = library[p];
        const int bank = library[p + 1];
        if (patch == 0xFF || bank == 0xFF) {
            break;
        }
        const std::size_t at = library[p + 2] | (library[p + 3] << 8) | (static_cast<std::size_t>(library[p + 4]) << 16) |
                               (static_cast<std::size_t>(library[p + 5]) << 24);
        // Each instrument: u16 size (14), the note shift, then 11 FM settings.
        if (at > library.size() || library.size() - at < 14) {
            continue;
        }
        Instrument in;
        in.transpose = static_cast<std::int8_t>(library[at + 2]);
        std::copy_n(library.begin() + static_cast<std::ptrdiff_t>(at + 3), 11, in.regs.begin());
        instruments_[bank << 8 | patch] = in;
    }
}

void AdLibMusic::setSongs(std::vector<XmidiSong> songs) {
    stop();
    songs_ = std::move(songs);
}

void AdLibMusic::play(int song) {
    stop();
    if (song < 0 || song >= songCount()) {
        return;
    }
    song_ = song;
    pos_ = 0;
    wait_ = 0;
    for (auto& c : channels_) {
        c = Channel{};
    }
}

void AdLibMusic::stop() {
    song_ = -1;
    notes_.clear();
    loops_.clear();
    silence();
}

void AdLibMusic::silence() {
    for (int i = 0; i < 9; ++i) {
        voices_[static_cast<std::size_t>(i)] = Voice{};
        write(0xB0 + i, 0);
        write(0x40 + kOperator[i] + 3, 0x3F);
        write(0x40 + kOperator[i], 0x3F);
    }
}

void AdLibMusic::render(std::int16_t* out, std::size_t frames) {
    const double samplesPerTick = static_cast<double>(rate_) / kTicksPerSecond;
    for (std::size_t i = 0; i < frames; ++i) {
        if (untilTick_ <= 0) {
            tick();
            untilTick_ += samplesPerTick;
        }
        untilTick_ -= 1;
        std::int16_t s[2];
        OPL3_GenerateResampled(chip_.get(), s);
        // The emulated chip is quiet next to the voices: twice as loud.
        out[2 * i] = static_cast<std::int16_t>(std::clamp(s[0] * 2 * master_ / 127, -32768, 32767));
        out[2 * i + 1] = static_cast<std::int16_t>(std::clamp(s[1] * 2 * master_ / 127, -32768, 32767));
    }
}

std::uint32_t AdLibMusic::readLength(std::size_t& p) const {
    const auto& ev = songs_[static_cast<std::size_t>(song_)].events;
    std::uint32_t v = 0;
    for (int i = 0; i < 4 && p < ev.size(); ++i) {
        const std::uint8_t b = ev[p++];
        v = (v << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) {
            break;
        }
    }
    return v;
}

void AdLibMusic::tick() {
    ++clock_;
    // Notes whose time is up.
    for (std::size_t i = 0; i < notes_.size();) {
        if (notes_[i].ticksLeft == 0 || --notes_[i].ticksLeft == 0) {
            const Playing n = notes_[i];
            notes_.erase(notes_.begin() + static_cast<std::ptrdiff_t>(i));
            noteOff(n.channel, n.note);
        } else {
            ++i;
        }
    }
    if (song_ < 0) {
        return;
    }
    if (wait_ == 0) {
        runEvents();
    }
    if (wait_ > 0) {
        --wait_;
    }
}

void AdLibMusic::runEvents() {
    int guard = 0;
    while (song_ >= 0 && wait_ == 0 && guard++ < 10000) {
        const auto& ev = songs_[static_cast<std::size_t>(song_)].events;
        if (pos_ >= ev.size()) {
            stop();
            return;
        }
        const std::uint8_t status = ev[pos_];
        if (status < 0x80) {
            // A pause: the sum of the bytes below 0x80 in a row, in 120ths
            // of a second.
            std::uint32_t ticks = 0;
            while (pos_ < ev.size() && ev[pos_] < 0x80) {
                ticks += ev[pos_++];
            }
            wait_ = ticks;
            return;
        }
        ++pos_;
        auto byte = [&]() -> int { return pos_ < ev.size() ? ev[pos_++] : 0; };
        const int ch = status & 0x0F;
        switch (status & 0xF0) {
        case 0x80: {
            const int note = byte();
            byte();
            noteOff(ch, note);
            break;
        }
        case 0x90: {
            const int note = byte();
            const int velocity = byte();
            const std::uint32_t length = readLength(pos_);
            if (velocity == 0) {
                noteOff(ch, note);
            } else {
                noteOn(ch, note, velocity);
                notes_.push_back(Playing{ch, note, length});
            }
            break;
        }
        case 0xA0:
            byte();
            byte();
            break;
        case 0xB0: {
            const int number = byte();
            const int value = byte();
            controller(ch, number, value);
            break;
        }
        case 0xC0:
            channels_[static_cast<std::size_t>(ch)].program = byte();
            break;
        case 0xD0:
            byte();
            break;
        case 0xE0: {
            const int lo = byte();
            const int hi = byte();
            channels_[static_cast<std::size_t>(ch)].bend = lo | (hi << 7);
            for (int i = 0; i < 9; ++i) {
                if (voices_[static_cast<std::size_t>(i)].on && voices_[static_cast<std::size_t>(i)].channel == ch) {
                    setPitch(i);
                }
            }
            break;
        }
        default:
            if (status == 0xFF) {
                const int type = byte();
                const std::uint32_t length = readLength(pos_);
                if (type == 0x2F) {
                    stop();
                    return;
                }
                pos_ += length;
            } else if (status == 0xF0 || status == 0xF7) {
                pos_ += readLength(pos_);
            }
            break;
        }
    }
}

void AdLibMusic::controller(int ch, int number, int value) {
    Channel& c = channels_[static_cast<std::size_t>(ch)];
    switch (number) {
    case 7:
        c.volume = value;
        for (int i = 0; i < 9; ++i) {
            if (voices_[static_cast<std::size_t>(i)].on && voices_[static_cast<std::size_t>(i)].channel == ch) {
                setLevel(i);
            }
        }
        break;
    case 11:
        c.expression = value;
        for (int i = 0; i < 9; ++i) {
            if (voices_[static_cast<std::size_t>(i)].on && voices_[static_cast<std::size_t>(i)].channel == ch) {
                setLevel(i);
            }
        }
        break;
    case 64:
        c.sustain = value >= 64;
        if (!c.sustain) {
            for (auto& v : voices_) {
                if (v.on && v.held && v.channel == ch) {
                    releaseVoice(v);
                }
            }
        }
        break;
    case 0x72: // the instrument bank for the next program changes
        c.bank = value;
        break;
    case 0x74: // the start of a repeated part; 0 repeats for ever
        loops_.push_back(Loop{pos_, value});
        if (loops_.size() > 4) {
            loops_.erase(loops_.begin());
        }
        break;
    case 0x75: // the end of a repeated part
        if (value >= 64 && !loops_.empty()) {
            Loop& l = loops_.back();
            if (l.count == 0) {
                pos_ = l.start;
            } else if (--l.count > 0) {
                pos_ = l.start;
            } else {
                loops_.pop_back();
            }
        }
        break;
    case 120:
    case 123:
        for (auto& v : voices_) {
            if (v.on && v.channel == ch) {
                releaseVoice(v);
            }
        }
        break;
    default:
        break;
    }
}

const AdLibMusic::Instrument* AdLibMusic::instrumentFor(int ch, int note, int& played) const {
    const Channel& c = channels_[static_cast<std::size_t>(ch)];
    if (ch == kPercussionChannel) {
        const auto it = instruments_.find(kPercussionBank << 8 | note);
        if (it == instruments_.end()) {
            return nullptr;
        }
        played = static_cast<std::uint8_t>(it->second.transpose);
        return &it->second;
    }
    auto it = instruments_.find(c.bank << 8 | c.program);
    if (it == instruments_.end()) {
        it = instruments_.find(c.program);
    }
    if (it == instruments_.end()) {
        return nullptr;
    }
    played = std::clamp(note + it->second.transpose, 0, 127);
    return &it->second;
}

void AdLibMusic::noteOn(int ch, int note, int velocity) {
    int played = note;
    const Instrument* in = instrumentFor(ch, note, played);
    if (in == nullptr) {
        return;
    }
    // A free voice, or else the one that has sounded longest.
    int pick = -1;
    for (int i = 0; i < 9; ++i) {
        if (!voices_[static_cast<std::size_t>(i)].on) {
            pick = i;
            break;
        }
    }
    if (pick < 0) {
        pick = 0;
        for (int i = 1; i < 9; ++i) {
            if (voices_[static_cast<std::size_t>(i)].age < voices_[static_cast<std::size_t>(pick)].age) {
                pick = i;
            }
        }
    }
    Voice& v = voices_[static_cast<std::size_t>(pick)];
    write(0xB0 + pick, 0); // key off first, so the note starts fresh
    v = Voice{true, false, ch, note, played, velocity, clock_, in};
    const int mod = kOperator[pick];
    const int car = mod + 3;
    const auto& r = in->regs;
    write(0x20 + mod, r[0]);
    write(0x60 + mod, r[2]);
    write(0x80 + mod, r[3]);
    write(0xE0 + mod, r[4] & 3);
    write(0xC0 + pick, r[5] & 0x0F);
    write(0x20 + car, r[6]);
    write(0x60 + car, r[8]);
    write(0x80 + car, r[9]);
    write(0xE0 + car, r[10] & 3);
    setLevel(pick);
    setPitch(pick);
}

void AdLibMusic::noteOff(int ch, int note) {
    for (auto& v : voices_) {
        if (v.on && !v.held && v.channel == ch && v.note == note) {
            if (channels_[static_cast<std::size_t>(ch)].sustain) {
                v.held = true;
            } else {
                releaseVoice(v);
            }
            return;
        }
    }
}

void AdLibMusic::releaseVoice(Voice& v) {
    const int index = static_cast<int>(&v - voices_.data());
    v.on = false;
    v.held = false;
    setPitch(index); // writes the key bit off
}

void AdLibMusic::setLevel(int index) {
    const Voice& v = voices_[static_cast<std::size_t>(index)];
    if (v.instrument == nullptr) {
        return;
    }
    const Channel& c = channels_[static_cast<std::size_t>(v.channel)];
    // How loud, 0..127: the note's velocity, the channel's volume and
    // expression together.
    const int loud = v.velocity * c.volume * c.expression / (127 * 127);
    auto scaled = [loud](int reg) {
        const int ksl = reg & 0xC0;
        const int level = 0x3F - (reg & 0x3F);
        return ksl | (0x3F - level * loud / 127);
    };
    const auto& r = v.instrument->regs;
    const int mod = kOperator[index];
    write(0x40 + mod + 3, scaled(r[7]));
    // With the two operators added (rather than one shaping the other),
    // both are heard and both get quieter.
    write(0x40 + mod, (r[5] & 1) != 0 ? scaled(r[1]) : r[1]);
}

void AdLibMusic::setPitch(int index) {
    const Voice& v = voices_[static_cast<std::size_t>(index)];
    const int ch = v.channel < 0 ? 0 : v.channel;
    const double bend = (channels_[static_cast<std::size_t>(ch)].bend - 0x2000) / 8192.0 * 2.0;
    const double freq = 440.0 * std::pow(2.0, (v.played + bend - 69) / 12.0);
    int block = 0;
    int fnum = 0;
    for (block = 0; block < 8; ++block) {
        fnum = static_cast<int>(std::lround(freq * static_cast<double>(1 << (20 - block)) / 49716.0));
        if (fnum < 1024) {
            break;
        }
    }
    block = std::min(block, 7);
    fnum = std::clamp(fnum, 0, 1023);
    write(0xA0 + index, fnum & 0xFF);
    write(0xB0 + index, (v.on ? 0x20 : 0) | (block << 2) | (fnum >> 8));
}

} // namespace helmsman::audio
