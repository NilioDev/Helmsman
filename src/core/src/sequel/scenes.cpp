// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/sequel/scenes.hpp"

#include "helmsman/data/animation.hpp"
#include "helmsman/data/movie.hpp"
#include "helmsman/data/reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace helmsman::sequel {

namespace {

constexpr int kRecord = 16;
// The original counts fades in clock ticks of the PC's BIOS: 18.2 a second.
constexpr double kTickMillis = 1000.0 / 18.2065;
constexpr int kFadeStep = 10; // percent per tick

namespace cmd {
constexpr int kAnimation = 0;
constexpr int kNothing = 2;
constexpr int kSound = 5;
constexpr int kEnd = 6;
constexpr int kFadeSpeed = 7;
constexpr int kFilm = 8;
} // namespace cmd

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

void centre(Platform& p, const std::vector<std::uint8_t>& picture, int width, int height) {
    const int x0 = (p.screen.width() - width) / 2;
    const int y0 = (p.screen.height() - height) / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            p.screen.set(x0 + x, y0 + y, picture[static_cast<std::size_t>(y) * width + x]);
        }
    }
}

} // namespace

Assets::Assets(GameSource& source)
    : source_(source), data_(Archive::open(source, "DATA")), sounds_(source, "DATA.VCC") {}

std::optional<std::vector<std::uint8_t>> Assets::uncompressed(std::string_view name) {
    return source_.read("UNCOMP/" + upper(name));
}

std::optional<PlainSound> Assets::sound(std::string_view name) const {
    try {
        return sounds_.sound(name);
    } catch (const FormatError&) {
        return std::nullopt;
    }
}

bool playFilm(Platform& p, std::vector<std::uint8_t> file) {
    Movie movie(std::move(file));
    p.screen.fill(0);
    p.pointerShown = false;
    p.pointerTargets.clear();
    p.takeSkip();
    double due = static_cast<double>(p.millis);
    while (movie.next()) {
        auto sound = movie.takeSound();
        if (!sound.empty() && movie.soundRate() > 0) {
            p.queueSound(SoundChannel::Voice, movie.soundRate(), std::move(sound));
        }
        p.palette = movie.palette();
        centre(p, movie.picture(), movie.width(), movie.height());
        due += movie.frameMicros() / 1000.0;
        do {
            p.waitFrame();
            if (p.quit || p.takeSkip()) {
                p.stopSound(SoundChannel::Voice);
                p.screen.fill(0);
                return false;
            }
        } while (static_cast<double>(p.millis) < due);
    }
    // Let the last of the sound play out.
    while (!p.quit && p.soundQueued[static_cast<int>(SoundChannel::Voice)] > 0.0) {
        if (p.takeSkip()) {
            p.stopSound(SoundChannel::Voice);
            break;
        }
        p.waitFrame();
    }
    p.screen.fill(0);
    return !p.quit;
}

bool playAnimation(Platform& p, std::vector<std::uint8_t> file, const std::function<bool(int)>& onFrame) {
    Animation anim(std::move(file));
    const Palette full = anim.palette();
    const int fps = std::max(1, anim.framesPerSecond());
    p.pointerShown = false;
    p.pointerTargets.clear();
    p.screen.fill(0);

    int brightness = 0;
    int target = 100;
    double nextTick = static_cast<double>(p.millis);
    auto fade = [&] {
        while (static_cast<double>(p.millis) >= nextTick) {
            nextTick += kTickMillis;
            if (brightness < target) {
                brightness = std::min(target, brightness + kFadeStep);
            } else if (brightness > target) {
                brightness = std::max(target, brightness - kFadeStep);
            }
        }
        p.palette = full.faded(brightness * 256 / 100);
    };

    const double start = static_cast<double>(p.millis);
    bool stopped = false;
    for (int frame = 0; frame < anim.frameCount() && !stopped; ++frame) {
        anim.next();
        if (onFrame(frame)) {
            stopped = true;
            break;
        }
        const double due = start + 1000.0 * (frame + 1) / fps;
        centre(p, anim.picture(), anim.width(), anim.height());
        do {
            fade();
            p.waitFrame();
            if (p.quit || onFrame(frame)) {
                stopped = true;
                break;
            }
        } while (static_cast<double>(p.millis) < due);
    }
    // Fade out, still listening for a skip.
    target = 0;
    while (!p.quit && brightness > 0) {
        fade();
        p.waitFrame();
        if (!stopped && onFrame(anim.frameCount() - 1)) {
            stopped = true;
        }
    }
    p.palette = full.faded(0);
    return !stopped && !p.quit;
}

std::vector<SceneStep> readSceneScript(std::span<const std::uint8_t> bytes) {
    std::vector<SceneStep> steps;
    Reader r(bytes);
    while (r.remaining() >= kRecord) {
        SceneStep s;
        const int frame = r.u16();
        s.frame = frame == 0xFFFF ? -1 : frame;
        s.command = r.u16();
        s.a = r.s16();
        s.b = r.s16();
        s.name = r.fixedText(8);
        while (!s.name.empty() && s.name.back() == ' ') {
            s.name.pop_back();
        }
        steps.push_back(std::move(s));
    }
    return steps;
}

namespace {

class Scene {
public:
    Scene(Platform& p, Assets& assets, std::vector<SceneStep> steps)
        : p_(p), assets_(assets), steps_(std::move(steps)) {}

    void play() { run(-1); }

private:
    // Runs the records due at `frame` (-1: nothing is playing). Returns true
    // when the player asked to skip.
    bool run(int frame);
    bool skipRequested() {
        if (p_.takeSkip()) {
            p_.stopSound(SoundChannel::Effect);
            skipToEnd();
            return true;
        }
        return false;
    }
    void skipToEnd() {
        while (at_ < steps_.size() && !(steps_[at_].command == cmd::kEnd && steps_[at_].a != 1)) {
            ++at_;
        }
    }

    Platform& p_;
    Assets& assets_;
    std::vector<SceneStep> steps_;
    std::size_t at_ = 0;
};

bool Scene::run(int frame) {
    if (frame >= 0 && skipRequested()) {
        return true;
    }
    while (at_ < steps_.size() && !p_.quit) {
        const SceneStep s = steps_[at_];
        if (frame >= 0 && s.frame != frame) {
            return false;
        }
        const bool idle = frame < 0;
        switch (s.command) {
        case cmd::kAnimation: {
            if (!idle) {
                return false;
            }
            ++at_;
            if (auto file = assets_.uncompressed(s.name + ".ANM")) {
                try {
                    playAnimation(p_, std::move(*file), [this](int f) { return run(f); });
                } catch (const FormatError&) {
                }
            }
            break;
        }
        case cmd::kFilm: {
            if (!idle) {
                return false;
            }
            ++at_;
            if (auto file = assets_.uncompressed(s.name + ".MVE")) {
                try {
                    if (!playFilm(p_, std::move(*file))) {
                        skipToEnd();
                    }
                } catch (const FormatError&) {
                }
            }
            break;
        }
        case cmd::kSound:
            ++at_;
            if (auto sound = assets_.sound(s.name)) {
                p_.queueSound(SoundChannel::Effect, sound->rate, std::move(sound->samples), true);
            }
            break;
        case cmd::kEnd:
            if (s.a == 1) {
                at_ = 0; // round again
            } else if (s.a == 2) {
                at_ = steps_.size();
                return false;
            } else {
                ++at_;
            }
            break;
        case cmd::kFadeSpeed:
        case cmd::kNothing:
        default:
            ++at_;
            break;
        }
    }
    return false;
}

} // namespace

void playScene(Platform& platform, Assets& assets, std::string_view name) {
    const auto bytes = assets.data().load(upper(name) + ".DEM");
    if (!bytes) {
        return;
    }
    Scene scene(platform, assets, readSceneScript(*bytes));
    platform.takeSkip();
    scene.play();
}

} // namespace helmsman::sequel
