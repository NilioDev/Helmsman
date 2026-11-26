// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arithmetic of space: positions are whole numbers, directions are
// 3x3 matrices of fractions with 14 bits after the point (16384 is one),
// and angles are counted in 1024ths of a turn. Working in whole numbers
// the way the game does keeps ships moving and turning just as there.
// See docs/rules/space.md.
#pragma once

#include <array>
#include <cstdint>

namespace helmsman::space {

constexpr int kOne = 16384;     // 1.0 in a matrix
constexpr int kTurn = 1024;     // a whole turn, in angle units

struct Vec {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    Vec operator+(const Vec& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec operator-(const Vec& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec operator-() const { return {-x, -y, -z}; }
    bool operator==(const Vec&) const = default;
};

// Rows: the right, up and forward directions of a thing, in the frame
// around it.
struct Mat {
    std::array<std::int16_t, 9> m{};

    std::int16_t& at(int row, int col) { return m[static_cast<std::size_t>(row * 3 + col)]; }
    std::int16_t at(int row, int col) const { return m[static_cast<std::size_t>(row * 3 + col)]; }
    Vec row(int r) const { return {at(r, 0), at(r, 1), at(r, 2)}; }

    static Mat identity();
    bool operator==(const Mat&) const = default;
};

Mat operator*(const Mat& a, const Mat& b);
Mat transpose(const Mat& a);
// a times v: a vector of the outer frame in the frame of `a`'s rows.
Vec apply(const Mat& a, const Vec& v);
// a's transpose times v: back from `a`'s frame to the outer one.
Vec applyTransposed(const Mat& a, const Vec& v);

// Straight-line length (each part must stay under 32768).
std::int32_t length(const Vec& v);
// The quick estimate the ships use to judge distance.
std::int32_t roughLength(const Vec& v);
// v scaled to length one (16384), pointing the same way.
Vec unit(const Vec& v);
std::int32_t isqrt(std::int64_t v);

// Turning a frame by whole steps of a small angle about its own right
// (pitch), up (yaw) or forward (roll) direction. `steps` may be negative;
// at most 32 steps of 1/1024 of a radian a tick.
void pitch(Mat& a, int steps);
void yaw(Mat& a, int steps);
void roll(Mat& a, int steps);
// Makes the rows of a frame square and of length one again.
void tidy(Mat& a);

// A turn of `a` steps, in angle units with 16 bits after the point.
std::int32_t stepAngle(int steps);

// Angles, in 1024ths of a turn.
int angleOf(double y, double x);  // like atan2, 0..1023

} // namespace helmsman::space
