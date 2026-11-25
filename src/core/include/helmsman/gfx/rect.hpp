// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helmsman {

// A rectangle; right and bottom are one past the last column and row.
struct ClipRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool empty() const { return right <= left || bottom <= top; }
    bool operator==(const ClipRect&) const = default;
};

} // namespace helmsman
