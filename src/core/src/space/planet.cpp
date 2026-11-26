// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/space/planet.hpp"

#include "helmsman/data/reader.hpp"

#include <algorithm>

namespace helmsman::space {

namespace {

constexpr int kSide = PlanetLook::kSide;
constexpr std::size_t kPoints = static_cast<std::size_t>(kSide) * kSide;

// Where the colour ramps lie in the first game's program, as loaded.
constexpr std::size_t kRampsAt = 0x34720;

// PLANET.DAT: first a table of kSide/2 x kSide 16-bit surface positions,
// then for each size a square of 16-bit entries into that table (an entry
// with its top bit set is outside the disc).
constexpr std::size_t kTurnTable = 0x4000;

std::size_t discAt(int size) {
    std::size_t at = kTurnTable;
    for (int i = 0; i < size; ++i) {
        const auto n = static_cast<std::size_t>(kPlanetPixels[static_cast<std::size_t>(i)]);
        at += n * n * 2;
    }
    return at;
}

std::uint16_t word(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

// The C library's random numbers that the game seeds with the planet's
// number, so that each planet comes out as it always does.
class Chance {
public:
    explicit Chance(int seed) : state_(static_cast<std::uint32_t>(seed) & 0xFFFF) {}
    int next() {
        state_ = state_ * 214013u + 2531011u;
        return static_cast<int>((state_ >> 16) & 0x7FFF);
    }

private:
    std::uint32_t state_;
};

class Surface16 {
public:
    explicit Surface16(int seed) : chance_(seed), h_(kPoints, 0) {}

    std::int16_t& at(int x, int y) { return h_[static_cast<std::size_t>(x * kSide + y)]; }

    // The middle of the line from (x0, y0) to (x1, y1) becomes the mean of
    // its ends moved by chance by less than the line is long; then the same
    // for both halves. The far end wraps round the surface.
    void line(int x0, int y0, int x1, int y1) {
        const int hx = (x1 - x0) >> 1;
        const int hy = (y1 - y0) >> 1;
        const int reach = hx + hy;
        if (reach <= 0) {
            return;
        }
        const auto a = at(x0, y0);
        const auto b = at(x1 & (kSide - 1), y1 & (kSide - 1));
        auto mean = static_cast<std::int16_t>(static_cast<std::int16_t>(a + b) / 2);
        const auto shove = static_cast<std::int16_t>(chance_.next() - 0x4000);
        mean = static_cast<std::int16_t>(mean + shove % reach);
        at(x0 + hx, y0 + hy) = mean;
        if (reach >= 2) {
            line(x0, y0, x0 + hx, y0 + hy);
            line(x0 + hx, y0 + hy, x1, y1);
        }
    }

    // A square: its middle row, then its middle column, then each quarter.
    void square(int x0, int y0, int x1, int y1) {
        const int hx = (x1 - x0) >> 1;
        const int hy = (y1 - y0) >> 1;
        line(x0, y0 + hy, x1, y0 + hy);
        if (hx + hy > 2) {
            line(x0 + hx, y0, x0 + hx, y0 + hy);
            line(x0 + hx, y0 + hy, x0 + hx, y1);
        }
        if (hx + hy >= 4) {
            square(x0, y0, x0 + hx, y0 + hy);
            square(x0 + hx, y0, x1, y0 + hy);
            square(x0, y0 + hy, x0 + hx, y1);
            square(x0 + hx, y0 + hy, x1, y1);
        }
    }

    const std::vector<std::int16_t>& heights() const { return h_; }

private:
    Chance chance_;
    std::vector<std::int16_t> h_;
};

} // namespace

PlanetLook PlanetLook::fromBytes(std::span<const std::uint8_t> table, std::span<const std::uint8_t> program) {
    PlanetLook look;
    if (table.size() < discAt(kSizes)) {
        throw FormatError("PLANET.DAT is too short");
    }
    look.table.assign(table.begin(), table.end());
    const std::size_t need = kRampsAt + static_cast<std::size_t>(kRamps) * kShades * 2;
    if (program.size() < need) {
        throw FormatError("the program has no colour ramps where expected");
    }
    for (int r = 0; r < kRamps; ++r) {
        for (int s = 0; s < kShades; ++s) {
            const std::uint16_t v = word(program, kRampsAt + static_cast<std::size_t>(r * kShades + s) * 2);
            if (v > 0xFF || v == 0) {
                throw FormatError("the program has no colour ramps where expected");
            }
            look.ramps[static_cast<std::size_t>(r)][static_cast<std::size_t>(s)] = static_cast<std::uint8_t>(v);
        }
    }
    return look;
}

std::shared_ptr<const PlanetLook> PlanetLook::load(const Archive& archive, std::span<const std::uint8_t> program) {
    const auto table = archive.load("PLANET.DAT");
    if (!table) {
        return nullptr;
    }
    try {
        return std::make_shared<const PlanetLook>(fromBytes(*table, program));
    } catch (const FormatError&) {
        return nullptr;
    }
}

std::vector<std::uint8_t> planetSurface(const PlanetLook& look, int seed, int ramp) {
    Surface16 s(seed);
    s.line(0, 0, kSide, 0);
    s.line(0, 0, 0, kSide);
    s.square(0, 0, kSide, kSide);
    const auto& h = s.heights();
    std::int16_t low = 0x7FFF;
    std::int16_t high = -0x8000;
    for (const std::int16_t v : h) {
        low = std::min(low, v);
        high = std::max(high, v);
    }
    const auto span = static_cast<std::int16_t>(high - low + 1);
    const auto& colours = look.ramps[static_cast<std::size_t>(std::clamp(ramp, 0, PlanetLook::kRamps - 1))];
    std::vector<std::uint8_t> out(kPoints);
    for (std::size_t i = 0; i < kPoints; ++i) {
        // In 16 bits, as the game works it out.
        const auto scaled = static_cast<std::int16_t>(static_cast<std::int16_t>(h[i] - low) * PlanetLook::kShades);
        int shade = span != 0 ? scaled / span : 0;
        shade = std::clamp(shade, 0, PlanetLook::kShades - 1);
        out[i] = colours[static_cast<std::size_t>(shade)];
    }
    return out;
}

Bitmap planetPicture(const PlanetLook& look, std::span<const std::uint8_t> surface, int size, int longitude,
                     int latitude) {
    size = std::clamp(size, 0, PlanetLook::kSizes - 1);
    const int n = kPlanetPixels[static_cast<std::size_t>(size)];
    Bitmap b;
    b.handleX = (n - 1) / 2;
    b.handleY = (n - 1) / 2;
    b.pixels = Surface(n, n);
    if (surface.size() < kPoints) {
        return b;
    }
    // A step of latitude moves a row of the turning table, one of longitude
    // a row of the surface; both in 128ths of a turn.
    auto steps = [](int angle) { return (((angle << 7) + 0x200) / 0x400) & 0x7F; };
    const std::size_t rowShift = static_cast<std::size_t>(steps(latitude)) << 7;
    const std::size_t spin = static_cast<std::size_t>(steps(longitude)) << 7;
    const std::size_t disc = discAt(size);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const std::uint16_t entry = word(look.table, disc + static_cast<std::size_t>(y * n + x) * 2);
            if (entry & 0x8000) {
                continue;
            }
            const std::size_t inTurn = (entry + rowShift) & 0x3FFF;
            const std::size_t point = (word(look.table, inTurn) + spin) & 0x3FFF;
            b.pixels.set(x, y, surface[point]);
        }
    }
    return b;
}

} // namespace helmsman::space
