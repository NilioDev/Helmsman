// SPDX-License-Identifier: GPL-3.0-or-later
//
// Disc images kept in a .zip: unpacked once into a cache folder, so the
// image inside can be read like any other (see zip.cpp).
#pragma once

#include <filesystem>

namespace helmsman {

// Whether `path` is a zip archive by its name: .zip, or the first part of a
// split one (.zip.001, also .001 alone when its bytes begin as a zip's do).
bool isZipName(const std::filesystem::path& path);

// Unpacks the archive into a folder of its own below `cacheRoot` and returns
// that folder. An archive unpacked before (same name, size and time) is not
// unpacked again. Throws FormatError when the archive is damaged, uses a
// kind of packing other than plain or deflate, or is locked with a password.
std::filesystem::path unpackZip(const std::filesystem::path& zip, const std::filesystem::path& cacheRoot);

} // namespace helmsman
