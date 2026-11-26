/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Builds the music decoders kept in third_party (each is one file that
 * holds its own implementation). Helmsman uses them for disc images that
 * keep the CD's music as .ogg or .mp3 files. (.flac uses the dr_flac that
 * libchdr builds.) */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis/stb_vorbis.c"
