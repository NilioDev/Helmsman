// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Helmsman app: the SDL3 platform layer. It opens a window and the
// sound device, keeps the list of the player's games, and runs Helmsman
// (the choice of games, then the game) on its own thread (see
// helmsman/engine/fiber.hpp), one step per frame. Each frame it hands the
// game the clock, mouse, keys and gamepad, then shows the game's screen
// scaled sharply to the window and plays the sound the game queued.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "helmsman/data/game_source.hpp"
#include "helmsman/data/reader.hpp"
#include "helmsman/engine/fiber.hpp"
#include "helmsman/engine/pad_buttons.hpp"
#include "helmsman/engine/platform.hpp"
#include "helmsman/engine/play.hpp"
#include "helmsman/engine/pointer_jump.hpp"
#include "helmsman/engine/shell.hpp"
#include "helmsman/version.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr int kScreenW = 320;
constexpr int kScreenH = 200;
constexpr int kChannels = static_cast<int>(helmsman::SoundChannel::Count);
// The games drew for screens with tall pixels: 320 by 200 filled a 4:3
// screen, so each pixel shows 1.2 times as tall as wide.
constexpr int kShownH = 240;

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioDeviceID audio = 0;
    std::array<SDL_AudioStream*, kChannels> streams{};
    std::array<int, kChannels> streamRates{};
    helmsman::Platform platform;
    helmsman::Library library;
    std::unique_ptr<helmsman::Fiber> program;
    Uint64 startNs = 0;
    Uint64 lastFrameNs = 0;
    std::vector<Uint32> rgba = std::vector<Uint32>(static_cast<std::size_t>(kScreenW) * kScreenH);
    helmsman::Surface shown;
    bool systemCursor = true;
    // The gamepad's pointer: used instead of the mouse's from the moment
    // the gamepad moves it or clicks, until the mouse moves again.
    bool padPointer = false;
    float padX = 0;
    float padY = 0;
    std::uint8_t padButtons = 0;
    helmsman::PadButtons buttons;
    // The games the player added, by what they gave (image or folder).
    std::vector<std::filesystem::path> added;
    // Games found, by game: refreshed when the list changes.
    std::vector<std::pair<helmsman::GameId, std::filesystem::path>> found;
    std::mutex droppedLock;
    std::vector<std::string> dropped;

    ~App() {
        program.reset(); // stop the game thread before what it uses goes away
        for (auto* stream : streams) {
            if (stream != nullptr) {
                SDL_DestroyAudioStream(stream);
            }
        }
        if (audio != 0) {
            SDL_CloseAudioDevice(audio);
        }
    }
};

std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string toUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path prefFolder() {
    std::filesystem::path path;
    if (char* pref = SDL_GetPrefPath("NilioDev", "Helmsman")) {
        path = pref;
        SDL_free(pref);
    }
    return path;
}

// Folders searched for the games: those given, gamedata beside the program
// (or beside the AppImage), in the working folder, and in Helmsman's own.
std::vector<std::filesystem::path> gameFolders(int argc, char** argv) {
    std::vector<std::filesystem::path> folders;
    for (int i = 1; i < argc; ++i) {
        folders.emplace_back(argv[i]);
    }
    if (const char* appImage = SDL_getenv("APPIMAGE")) {
        folders.emplace_back(std::filesystem::path(appImage).parent_path() / "gamedata");
    }
    if (const char* base = SDL_GetBasePath()) {
        folders.emplace_back(std::filesystem::path(base) / "gamedata");
    }
    folders.emplace_back("gamedata");
    folders.emplace_back(prefFolder() / "gamedata");
    return folders;
}

void readAdded(App& app) {
    std::ifstream in(prefFolder() / "games.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            app.added.emplace_back(fromUtf8(line));
        }
    }
}

void writeAdded(const App& app) {
    std::ofstream out(prefFolder() / "games.txt", std::ios::trunc);
    for (const auto& path : app.added) {
        out << toUtf8(path) << '\n';
    }
}

// Tries a path as a game; remembers it when it is one.
bool tryGame(App& app, const std::filesystem::path& path) {
    try {
        auto source = helmsman::GameSource::open(path);
        if (!source) {
            return false;
        }
        const auto game = source->game();
        for (const auto& [g, p] : app.found) {
            if (g == game) {
                return true; // the first found stays
            }
        }
        app.found.emplace_back(game, path);
        return true;
    } catch (const std::exception& e) {
        SDL_Log("%s: %s", path.string().c_str(), e.what());
        return false;
    }
}

void findGames(App& app, int argc, char** argv) {
    app.found.clear();
    for (const auto& path : app.added) {
        tryGame(app, path);
    }
    for (const auto& folder : gameFolders(argc, argv)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(folder, ec)) {
            tryGame(app, folder);
            continue;
        }
        if (!std::filesystem::is_directory(folder, ec)) {
            continue;
        }
        if (tryGame(app, folder)) {
            continue;
        }
        // Images and folders inside it: a .cue before its .bin.
        std::vector<std::filesystem::path> inside;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            inside.push_back(entry.path());
        }
        std::sort(inside.begin(), inside.end(), [](const auto& a, const auto& b) {
            const bool cueA = a.extension() == ".cue" || a.extension() == ".CUE";
            const bool cueB = b.extension() == ".cue" || b.extension() == ".CUE";
            return cueA != cueB ? cueA : a < b;
        });
        for (const auto& path : inside) {
            tryGame(app, path);
        }
    }
}

void openAudio(App& app) {
    app.audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (app.audio == 0) {
        SDL_Log("no sound: %s", SDL_GetError());
        return;
    }
    for (int i = 0; i < kChannels; ++i) {
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 22050};
        app.streams[static_cast<std::size_t>(i)] = SDL_CreateAudioStream(&spec, nullptr);
        app.streamRates[static_cast<std::size_t>(i)] = spec.freq;
        SDL_BindAudioStream(app.audio, app.streams[static_cast<std::size_t>(i)]);
    }
}

void playQueuedSound(App& app) {
    for (auto& command : app.platform.sound) {
        const auto channel = static_cast<std::size_t>(command.channel);
        SDL_AudioStream* stream = app.streams[channel];
        if (stream == nullptr) {
            continue;
        }
        if (command.stop) {
            SDL_ClearAudioStream(stream);
        }
        if (command.rate <= 0) {
            continue;
        }
        if (command.rate != app.streamRates[channel]) {
            const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, command.rate};
            SDL_SetAudioStreamFormat(stream, &spec, nullptr);
            app.streamRates[channel] = command.rate;
        }
        if (!command.wide.empty()) {
            SDL_PutAudioStreamData(stream, command.wide.data(), static_cast<int>(command.wide.size() * 2));
        }
        if (!command.samples.empty()) {
            std::vector<Sint16> frames(command.samples.size() * 2);
            for (std::size_t i = 0; i < command.samples.size(); ++i) {
                const auto v = static_cast<Sint16>((static_cast<int>(command.samples[i]) - 128) * 256);
                frames[2 * i] = v;
                frames[2 * i + 1] = v;
            }
            SDL_PutAudioStreamData(stream, frames.data(), static_cast<int>(frames.size() * 2));
        }
    }
    app.platform.sound.clear();
}

// --- The gamepad (see docs/gamepad.md)

void takePadPointer(App& app) {
    if (!app.padPointer) {
        app.padPointer = true;
        app.padX = static_cast<float>(app.platform.mouseX);
        app.padY = static_cast<float>(app.platform.mouseY);
    }
}

// The left stick moves the pointer freely, faster the further it is pushed.
void movePadPointer(App& app, float seconds) {
    constexpr float kDeadZone = 8000.0f;
    constexpr float kFullSpeed = 220.0f; // screen pixels a second
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    float ax = 0;
    float ay = 0;
    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr) {
            continue;
        }
        const float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const float y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (std::fabs(x) > kDeadZone || std::fabs(y) > kDeadZone) {
            ax = x / 32767.0f;
            ay = y / 32767.0f;
        }
    }
    SDL_free(ids);
    if (ax == 0 && ay == 0) {
        return;
    }
    takePadPointer(app);
    const auto curve = [](float v) { return v * std::fabs(v); };
    app.padX = std::clamp(app.padX + curve(ax) * kFullSpeed * seconds, 0.0f, kScreenW - 1.0f);
    app.padY = std::clamp(app.padY + curve(ay) * kFullSpeed * seconds, 0.0f, kScreenH - 1.0f);
}

// The direction pad jumps to the next thing that can be clicked that way.
void jumpPadPointer(App& app, int dx, int dy) {
    takePadPointer(app);
    const auto to = helmsman::jumpTarget(app.platform.pointerTargets, static_cast<int>(app.padX),
                                         static_cast<int>(app.padY), dx, dy);
    if (to) {
        app.padX = static_cast<float>(to->x);
        app.padY = static_cast<float>(to->y);
    }
}

void readPadButtons(App& app) {
    std::ifstream in(prefFolder() / "gamepad.txt", std::ios::binary);
    if (in) {
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        const int skipped = app.buttons.read(text);
        if (skipped > 0) {
            SDL_Log("gamepad.txt: %d lines left out", skipped);
        }
    }
}

// Window coordinates to the game's 320 by 200.
void toScreen(App& app, float wx, float wy, float& x, float& y) {
    SDL_RenderCoordinatesFromWindow(app.renderer, wx, wy, &x, &y);
    y = y * kScreenH / kShownH;
}

void updateInput(App& app) {
    auto& p = app.platform;
    const Uint64 now = SDL_GetTicksNS();
    p.millis = (now - app.startNs) / 1000000;
    if (p.warpPointer) {
        p.warpPointer = false;
        p.mouseX = p.warpX;
        p.mouseY = p.warpY;
        if (app.padPointer) {
            app.padX = static_cast<float>(p.warpX);
            app.padY = static_cast<float>(p.warpY);
        } else {
            float wx = 0;
            float wy = 0;
            SDL_RenderCoordinatesToWindow(app.renderer, static_cast<float>(p.warpX) + 0.5f,
                                          (static_cast<float>(p.warpY) + 0.5f) * kShownH / kScreenH, &wx, &wy);
            SDL_WarpMouseInWindow(app.window, wx, wy);
        }
    }
    float wx = 0;
    float wy = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&wx, &wy);
    float x = 0;
    float y = 0;
    toScreen(app, wx, wy, x, y);
    const float seconds = app.lastFrameNs == 0 ? 0.0f : static_cast<float>(now - app.lastFrameNs) / 1e9f;
    app.lastFrameNs = now;
    movePadPointer(app, seconds);
    if (app.padPointer) {
        p.mouseX = std::clamp(static_cast<int>(app.padX), 0, kScreenW - 1);
        p.mouseY = std::clamp(static_cast<int>(app.padY), 0, kScreenH - 1);
    } else {
        p.mouseX = std::clamp(static_cast<int>(std::floor(x)), 0, kScreenW - 1);
        p.mouseY = std::clamp(static_cast<int>(std::floor(y)), 0, kScreenH - 1);
    }
    p.padInUse = app.padPointer;
    p.mouseButtons = static_cast<std::uint8_t>(((buttons & SDL_BUTTON_LMASK) != 0 ? 1 : 0) |
                                               ((buttons & SDL_BUTTON_RMASK) != 0 ? 2 : 0) | app.padButtons);
    for (int i = 0; i < kChannels; ++i) {
        SDL_AudioStream* stream = app.streams[static_cast<std::size_t>(i)];
        const int rate = app.streamRates[static_cast<std::size_t>(i)];
        p.soundQueued[i] = stream != nullptr && rate > 0 ? SDL_GetAudioStreamQueued(stream) / (rate * 4.0) : 0.0;
    }
}

void drawScreen(App& app) {
    auto& p = app.platform;
    helmsman::Surface& screen = app.shown;
    screen = p.screen;
    if (p.pointerShown && p.pointer.width() > 0) {
        for (int row = 0; row < p.pointer.height(); ++row) {
            for (int col = 0; col < p.pointer.width(); ++col) {
                const auto c = p.pointer.at(col, row);
                if (c != 0) {
                    screen.set(p.mouseX - p.pointerHotX + col, p.mouseY - p.pointerHotY + row, c);
                }
            }
        }
    }
    // The game draws its own pointer; the system's is hidden while it shows.
    if (p.pointerShown == app.systemCursor) {
        app.systemCursor = !p.pointerShown;
        if (app.systemCursor) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
    std::array<Uint32, 256> colours{};
    for (std::size_t i = 0; i < colours.size(); ++i) {
        const auto& c = p.palette[i];
        colours[i] = 0xFF000000u | (static_cast<Uint32>(helmsman::Palette::to8Bit(c.r)) << 16) |
                     (static_cast<Uint32>(helmsman::Palette::to8Bit(c.g)) << 8) | helmsman::Palette::to8Bit(c.b);
    }
    for (std::size_t i = 0; i < app.rgba.size() && i < screen.size(); ++i) {
        app.rgba[i] = colours[screen.data()[i]];
    }
    SDL_UpdateTexture(app.texture, nullptr, app.rgba.data(), kScreenW * 4);
    SDL_RenderTexture(app.renderer, app.texture, nullptr, nullptr);
}

int scaleToFit(const SDL_Rect& usable) {
    if (usable.w <= 0 || usable.h <= 0) {
        return 3;
    }
    const int across = usable.w * 9 / 10 / kScreenW;
    const int down = usable.h * 9 / 10 / kShownH;
    return std::max(1, std::min(across, down));
}

SDL_Rect usableBounds(SDL_DisplayID display) {
    SDL_Rect usable{};
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable)) {
        usable = SDL_Rect{0, 0, 0, 0};
    }
    return usable;
}

void fitWindow(SDL_Window* window) {
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0) {
        return;
    }
    const SDL_Rect usable = usableBounds(SDL_GetDisplayForWindow(window));
    if (usable.w <= 0) {
        return;
    }
    const int scale = scaleToFit(usable);
    const int w = kScreenW * scale;
    const int h = kShownH * scale;
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, usable.x + (usable.w - w) / 2, usable.y + (usable.h - h) / 2);
}

bool isModifier(SDL_Keycode key) {
    switch (key) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
    case SDLK_MODE:
    case SDLK_CAPSLOCK:
    case SDLK_NUMLOCKCLEAR:
    case SDLK_SCROLLLOCK:
        return true;
    default:
        return false;
    }
}

// The PC scancode for a key, as the games read the keyboard.
std::uint8_t pcScancode(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_1: return 0x02;
    case SDL_SCANCODE_2: return 0x03;
    case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05;
    case SDL_SCANCODE_5: return 0x06;
    case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08;
    case SDL_SCANCODE_8: return 0x09;
    case SDL_SCANCODE_9: return 0x0A;
    case SDL_SCANCODE_0: return 0x0B;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_Q: return 0x10;
    case SDL_SCANCODE_W: return 0x11;
    case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_T: return 0x14;
    case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16;
    case SDL_SCANCODE_I: return 0x17;
    case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return 0x1C;
    case SDL_SCANCODE_A: return 0x1E;
    case SDL_SCANCODE_S: return 0x1F;
    case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_F: return 0x21;
    case SDL_SCANCODE_G: return 0x22;
    case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_J: return 0x24;
    case SDL_SCANCODE_K: return 0x25;
    case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_Z: return 0x2C;
    case SDL_SCANCODE_X: return 0x2D;
    case SDL_SCANCODE_C: return 0x2E;
    case SDL_SCANCODE_V: return 0x2F;
    case SDL_SCANCODE_B: return 0x30;
    case SDL_SCANCODE_N: return 0x31;
    case SDL_SCANCODE_M: return 0x32;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_F1: return 0x3B;
    case SDL_SCANCODE_F2: return 0x3C;
    case SDL_SCANCODE_F3: return 0x3D;
    case SDL_SCANCODE_F4: return 0x3E;
    case SDL_SCANCODE_F5: return 0x3F;
    case SDL_SCANCODE_F6: return 0x40;
    case SDL_SCANCODE_F7: return 0x41;
    case SDL_SCANCODE_F8: return 0x42;
    case SDL_SCANCODE_F9: return 0x43;
    case SDL_SCANCODE_F10: return 0x44;
    case SDL_SCANCODE_HOME:
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_PAGEUP:
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_END:
    case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_PAGEDOWN:
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_INSERT: return 0x52;
    case SDL_SCANCODE_DELETE: return 0x53;
    default: return 0;
    }
}

void startProgram(App& app, int argc, char** argv) {
    app.library.games = [&app] {
        std::vector<helmsman::GameEntry> games;
        for (const auto& [game, path] : app.found) {
            games.push_back(helmsman::GameEntry{game, toUtf8(path.filename())});
        }
        return games;
    };
    app.library.open = [&app](helmsman::GameId game) -> std::unique_ptr<helmsman::GameSource> {
        for (const auto& [g, path] : app.found) {
            if (g == game) {
                return helmsman::GameSource::open(path);
            }
        }
        return nullptr;
    };
    app.library.play = [&app](helmsman::GameSource& source) { helmsman::playGame(app.platform, source); };
    findGames(app, argc, argv);
    app.platform.saveFolder = prefFolder() / "saves";
    app.program = std::make_unique<helmsman::Fiber>([&app] { helmsman::runShell(app.platform, app.library); });
    app.platform.fiber = app.program.get();
}

// Games dropped on the window are added, and remembered.
void takeDropped(App& app) {
    std::vector<std::string> dropped;
    {
        const std::lock_guard lock(app.droppedLock);
        dropped.swap(app.dropped);
    }
    for (const auto& text : dropped) {
        const auto path = fromUtf8(text);
        try {
            auto source = helmsman::GameSource::open(path);
            if (!source) {
                app.library.note = "That is not one of the two games' CDs.";
                continue;
            }
            const auto game = source->game();
            std::erase_if(app.found, [game](const auto& f) { return f.first == game; });
            app.found.emplace_back(game, path);
            std::erase_if(app.added, [&path](const auto& p) { return p == path; });
            app.added.push_back(path);
            writeAdded(app);
            app.library.note.clear();
        } catch (const std::exception& e) {
            app.library.note = e.what();
        }
    }
}

} // namespace

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    auto app = std::make_unique<App>();
    SDL_SetAppMetadata("Helmsman", helmsman::kVersion, "io.github.niliodev.helmsman");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    const int scale = scaleToFit(usableBounds(SDL_GetPrimaryDisplay()));
    if (!SDL_CreateWindowAndRenderer("Helmsman", kScreenW * scale, kShownH * scale, SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        SDL_Log("cannot open a window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    fitWindow(app->window);
    SDL_SetRenderLogicalPresentation(app->renderer, kScreenW, kShownH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(app->renderer, 1);
    app->texture =
        SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);
    openAudio(*app);
    readPadButtons(*app);
    readAdded(*app);
    app->startNs = SDL_GetTicksNS();
    startProgram(*app, argc, argv);
    *state = app.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    auto* app = static_cast<App*>(state);
    auto& p = app->platform;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data != nullptr) {
            const std::lock_guard lock(app->droppedLock);
            app->dropped.emplace_back(event->drop.data);
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_KEY_DOWN: {
        const auto& key = event->key;
        const bool enter = key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER;
        if (key.key == SDLK_F11 || (enter && (key.mod & SDL_KMOD_ALT) != 0)) {
            if (!key.repeat) {
                const bool full = (SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(app->window, !full);
            }
            return SDL_APP_CONTINUE;
        }
        if (isModifier(key.key)) {
            return SDL_APP_CONTINUE;
        }
        char text = 0;
        if (key.key >= 32 && key.key < 127) {
            text = static_cast<char>(key.key);
            if ((key.mod & SDL_KMOD_SHIFT) != 0 && text >= 'a' && text <= 'z') {
                text = static_cast<char>(text - 'a' + 'A');
            }
        }
        p.keys.push_back(helmsman::KeyPress{pcScancode(key.scancode), text});
        return SDL_APP_CONTINUE;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(event->gdevice.which);
        return SDL_APP_CONTINUE;
    case SDL_EVENT_MOUSE_MOTION:
        app->padPointer = false;
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (app->buttons.action(event->gbutton.button)) {
        case helmsman::PadAction::Quit: return SDL_APP_SUCCESS;
        case helmsman::PadAction::Up: jumpPadPointer(*app, 0, -1); break;
        case helmsman::PadAction::Down: jumpPadPointer(*app, 0, 1); break;
        case helmsman::PadAction::Left: jumpPadPointer(*app, -1, 0); break;
        case helmsman::PadAction::Right: jumpPadPointer(*app, 1, 0); break;
        case helmsman::PadAction::Click:
            takePadPointer(*app);
            app->padButtons |= 1;
            break;
        case helmsman::PadAction::RightClick:
            takePadPointer(*app);
            app->padButtons |= 2;
            break;
        case helmsman::PadAction::Key: p.keys.push_back(helmsman::KeyPress{helmsman::scancode::kSpace, ' '}); break;
        case helmsman::PadAction::Menu: p.keys.push_back(helmsman::KeyPress{helmsman::scancode::kEscape, 27}); break;
        case helmsman::PadAction::None: break;
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        switch (app->buttons.action(event->gbutton.button)) {
        case helmsman::PadAction::Click: app->padButtons &= static_cast<std::uint8_t>(~1u); break;
        case helmsman::PadAction::RightClick: app->padButtons &= static_cast<std::uint8_t>(~2u); break;
        default: break;
        }
        return SDL_APP_CONTINUE;
    default:
        return SDL_APP_CONTINUE;
    }
}

SDL_AppResult SDL_AppIterate(void* state) {
    auto* app = static_cast<App*>(state);
    takeDropped(*app);
    updateInput(*app);
    try {
        app->program->resume();
    } catch (const std::exception& e) {
        SDL_Log("Helmsman stopped: %s", e.what());
        return SDL_APP_FAILURE;
    }
    playQueuedSound(*app);
    if (app->program->finished() || app->platform.quit) {
        return SDL_APP_SUCCESS;
    }
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    drawScreen(*app);
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) { delete static_cast<App*>(state); }
