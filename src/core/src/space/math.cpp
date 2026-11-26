// SPDX-License-Identifier: GPL-3.0-or-later
#include "helmsman/space/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace helmsman::space {

namespace {

// Each step of turning is 16/16384 of a radian; up to 32 steps a tick.
constexpr int kMaxSteps = 32;

struct Tables {
    std::array<std::int16_t, kMaxSteps + 1> cosine{};
    std::array<std::int16_t, kMaxSteps + 1> sine{};
    std::array<std::int32_t, kMaxSteps + 1> angle{};
    Tables() {
        for (int i = 0; i <= kMaxSteps; ++i) {
            const double a = i * 16.0 / 16384.0;
            sine[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::sin(a) * kOne);
            cosine[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::cos(a) * kOne);
            angle[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(512.0 * a / std::numbers::pi * 65536.0);
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

std::int16_t clamp16(std::int64_t v) { return static_cast<std::int16_t>(std::clamp<std::int64_t>(v, -32768, 32767)); }

// Turns a frame by `r` (a rotation in the frame's own terms): r times a.
void turn(Mat& a, int steps, int axisA, int axisB) {
    if (steps == 0) {
        return;
    }
    const int n = std::min(std::abs(steps), kMaxSteps);
    const std::int32_t c = tables().cosine[static_cast<std::size_t>(n)];
    const std::int32_t s = steps < 0 ? -tables().sine[static_cast<std::size_t>(n)] : tables().sine[static_cast<std::size_t>(n)];
    // Rows axisA and axisB turn into each other.
    Mat r = Mat::identity();
    r.at(axisA, axisA) = static_cast<std::int16_t>(c);
    r.at(axisA, axisB) = static_cast<std::int16_t>(s);
    r.at(axisB, axisA) = static_cast<std::int16_t>(-s);
    r.at(axisB, axisB) = static_cast<std::int16_t>(c);
    a = r * a;
}

} // namespace

Mat Mat::identity() {
    Mat a;
    a.at(0, 0) = kOne;
    a.at(1, 1) = kOne;
    a.at(2, 2) = kOne;
    return a;
}

Mat operator*(const Mat& a, const Mat& b) {
    Mat out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::int32_t sum = 0;
            for (int k = 0; k < 3; ++k) {
                sum += static_cast<std::int32_t>(a.at(i, k)) * b.at(k, j);
            }
            out.at(i, j) = static_cast<std::int16_t>(sum >> 14);
        }
    }
    return out;
}

Mat transpose(const Mat& a) {
    Mat out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out.at(i, j) = a.at(j, i);
        }
    }
    return out;
}

Vec apply(const Mat& a, const Vec& v) {
    Vec out;
    std::int32_t* parts[3] = {&out.x, &out.y, &out.z};
    for (int i = 0; i < 3; ++i) {
        const std::int64_t sum = static_cast<std::int64_t>(a.at(i, 0)) * v.x + static_cast<std::int64_t>(a.at(i, 1)) * v.y +
                                 static_cast<std::int64_t>(a.at(i, 2)) * v.z;
        *parts[i] = static_cast<std::int32_t>(sum >> 14);
    }
    return out;
}

Vec applyTransposed(const Mat& a, const Vec& v) {
    Vec out;
    std::int32_t* parts[3] = {&out.x, &out.y, &out.z};
    for (int i = 0; i < 3; ++i) {
        const std::int64_t sum = static_cast<std::int64_t>(a.at(0, i)) * v.x + static_cast<std::int64_t>(a.at(1, i)) * v.y +
                                 static_cast<std::int64_t>(a.at(2, i)) * v.z;
        *parts[i] = static_cast<std::int32_t>(sum >> 14);
    }
    return out;
}

std::int32_t isqrt(std::int64_t v) {
    if (v <= 0) {
        return 0;
    }
    auto r = static_cast<std::int64_t>(std::sqrt(static_cast<double>(v)));
    while (r * r > v) {
        --r;
    }
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return static_cast<std::int32_t>(r);
}

std::int32_t length(const Vec& v) {
    return isqrt(static_cast<std::int64_t>(v.x) * v.x + static_cast<std::int64_t>(v.y) * v.y +
                 static_cast<std::int64_t>(v.z) * v.z);
}

std::int32_t roughLength(const Vec& v) {
    std::int64_t a = std::llabs(v.x);
    std::int64_t b = std::llabs(v.y);
    std::int64_t c = std::llabs(v.z);
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    if (b < c) std::swap(b, c);
    return static_cast<std::int32_t>(std::min<std::int64_t>(a + b / 3 + c / 4, 0x7FFFFFFF));
}

Vec unit(const Vec& v) {
    std::int64_t len = length(v);
    if (len == 0) {
        len = 1;
    }
    return {static_cast<std::int32_t>((static_cast<std::int64_t>(v.x) << 14) / len),
            static_cast<std::int32_t>((static_cast<std::int64_t>(v.y) << 14) / len),
            static_cast<std::int32_t>((static_cast<std::int64_t>(v.z) << 14) / len)};
}

void pitch(Mat& a, int steps) { turn(a, steps, 1, 2); }
void yaw(Mat& a, int steps) { turn(a, steps, 2, 0); }
void roll(Mat& a, int steps) { turn(a, steps, 0, 1); }

void tidy(Mat& a) {
    // Keep forward as it is, make up square to it, and right from both.
    double f[3], u[3], r[3];
    for (int i = 0; i < 3; ++i) {
        f[i] = a.at(2, i);
        u[i] = a.at(1, i);
    }
    auto norm = [](double* v) {
        const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) {
            for (int i = 0; i < 3; ++i) v[i] /= l;
        }
    };
    norm(f);
    const double d = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
    for (int i = 0; i < 3; ++i) u[i] -= d * f[i];
    norm(u);
    // right = up x forward (rows right, up, forward form a left-handed set
    // as the screen shows them: x right, y up, z into the screen).
    r[0] = u[1] * f[2] - u[2] * f[1];
    r[1] = u[2] * f[0] - u[0] * f[2];
    r[2] = u[0] * f[1] - u[1] * f[0];
    for (int i = 0; i < 3; ++i) {
        a.at(0, i) = clamp16(std::lround(r[i] * kOne));
        a.at(1, i) = clamp16(std::lround(u[i] * kOne));
        a.at(2, i) = clamp16(std::lround(f[i] * kOne));
    }
}

std::int32_t stepAngle(int steps) {
    const int n = std::min(std::abs(steps), kMaxSteps);
    return tables().angle[static_cast<std::size_t>(n)];
}

int angleOf(double y, double x) {
    double a = std::atan2(y, x) / (2 * std::numbers::pi) * kTurn;
    if (a < 0) {
        a += kTurn;
    }
    return static_cast<int>(a) & (kTurn - 1);
}

} // namespace helmsman::space
