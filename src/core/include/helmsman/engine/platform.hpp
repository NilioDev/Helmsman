// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a game sees of the computer it runs on: the clock, the mouse and
// keys, and queues of sound to play. The app fills in the input before it
// resumes the game, and shows the screen and plays the sound the game
// queued after it yields. See docs/architecture.md.
#pragma once

#include "helmsman/engine/fiber.hpp"
#include "helmsman/gfx/palette.hpp"
#include "helmsman/gfx/rect.hpp"
#include "helmsman/gfx/surface.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <vector>

namespace helmsman {

// Keys as the games know them: PC keyboard scancodes, and the character
// typed.
struct KeyPress {
    std::uint8_t scancode = 0;
    char text = 0;
};

namespace scancode {
inline constexpr std::uint8_t kEscape = 0x01;
inline constexpr std::uint8_t kEnter = 0x1C;
inline constexpr std::uint8_t kSpace = 0x39;
inline constexpr std::uint8_t kBackspace = 0x0E;
inline constexpr std::uint8_t kTab = 0x0F;
inline constexpr std::uint8_t kUp = 0x48;
inline constexpr std::uint8_t kDown = 0x50;
inline constexpr std::uint8_t kLeft = 0x4B;
inline constexpr std::uint8_t kRight = 0x4D;
inline constexpr std::uint8_t kF1 = 0x3B;
} // namespace scancode

enum class SoundChannel : std::uint8_t {
    Voice,  // speech: one at a time
    Effect, // sound effects
    Music,  // signed 16-bit stereo frames at the player's rate
    Count,
};

struct SoundCommand {
    SoundChannel channel = SoundChannel::Voice;
    bool stop = false; // drop what is queued on the channel first
    int rate = 0;
    std::vector<std::uint8_t> samples; // unsigned 8-bit mono (Voice, Effect)
    std::vector<std::int16_t> wide;    // signed 16-bit stereo, left first (Music)
};

struct Platform {
    // Set by the app: where saved games go.
    std::filesystem::path saveFolder;
    // Set by the app before each resume.
    std::uint64_t millis = 0; // since the app started
    int mouseX = 0;
    int mouseY = 0;
    std::uint8_t mouseButtons = 0; // bit 0 left, bit 1 right
    std::deque<KeyPress> keys;
    double soundQueued[static_cast<int>(SoundChannel::Count)] = {}; // seconds still to play
    bool padInUse = false; // the gamepad moves the pointer now

    // Set by the game, taken by the app.
    Surface screen;
    Palette palette;
    bool pointerShown = false;
    Surface pointer{0, 0}; // the mouse pointer's picture, colour 0 left out
    int pointerHotX = 0;
    int pointerHotY = 0;
    std::vector<SoundCommand> sound;
    // What can be clicked on the screen now, for a gamepad's pointer to
    // jump to (see pointer_jump.hpp).
    std::vector<ClipRect> pointerTargets;
    // The game asks the app to put the pointer somewhere (as the originals
    // move the mouse); the app takes it at the next frame.
    bool warpPointer = false;
    int warpX = 0;
    int warpY = 0;
    // The game wants to end the program.
    bool quit = false;

    Fiber* fiber = nullptr;
    // Called on each wait, before control goes back to the app: the music
    // players run here.
    std::function<void(Platform&)> onWait;

private:
    std::uint8_t lastButtons_ = 0;

public:
    // Waits for the next frame of the app. Throws StopRequested when the
    // app is closing.
    void waitFrame() {
        if (onWait) {
            onWait(*this);
        }
        if (fiber != nullptr) {
            fiber->yield();
        }
    }

    // Waits until `ms` milliseconds have passed.
    void waitMillis(std::uint64_t ms) {
        const std::uint64_t end = millis + ms;
        while (millis < end) {
            waitFrame();
        }
    }

    // The mouse buttons that went down since the last call.
    std::uint8_t takeClicks() {
        const auto pressed = static_cast<std::uint8_t>(mouseButtons & ~lastButtons_);
        lastButtons_ = mouseButtons;
        return pressed;
    }

    // True once when a key was pressed or a mouse button went down since
    // the last call: what skips a pause.
    bool takeSkip() {
        const bool pressed = !keys.empty() || takeClicks() != 0;
        keys.clear();
        return pressed;
    }

    void queueSound(SoundChannel channel, int rate, std::vector<std::uint8_t> samples, bool stopFirst = false) {
        if (rate > 0) {
            soundQueued[static_cast<int>(channel)] += static_cast<double>(samples.size()) / rate;
        }
        sound.push_back(SoundCommand{channel, stopFirst, rate, std::move(samples), {}});
    }

    void stopSound(SoundChannel channel) {
        sound.push_back(SoundCommand{channel, true, 0, {}, {}});
        soundQueued[static_cast<int>(channel)] = 0;
    }
};

} // namespace helmsman
