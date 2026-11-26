# Disc images

The games come on a CD. Its first track holds the data, with an ISO 9660
file system. The 1992 game's CD also has music tracks after that. Players
keep the CD in many kinds of image. Helmsman reads all of the ones below
and turns each into the same thing:

- a list of **stores**, which are byte sources: a plain file, a compressed
  `.chd`, or a music file decoded to CD samples;
- a list of **tracks**. Each track says which store it is in, where in that
  store it starts, how many bytes one sector takes there (the *stride*),
  how many bytes of each sector to skip before the data, how many sectors
  it has, and whether its music samples are high byte first.

After that, every kind is read the same way (`disc.cpp`). The first data
track is the file system's. At sector 16 it is checked for the 12 sync
bytes (`00`, ten times `FF`, `00`) that begin a whole ("raw") sector:

- When they are there, the data starts 16 bytes into each sector for mode 1,
  or 24 bytes for mode 2.
- When they are not, the sectors are taken to be 2048 bytes of data
  ("cooked").

Music tracks are 2352 bytes of 16-bit stereo samples at 44100 Hz for each
sector, the left sample first.

The code is in `src/core/src/data/disc*.cpp`.

## Cue sheet (`.cue`, and `.gog` / `.ins`)

A text file. Each `FILE "name" KIND` line names a file beside the sheet.
Helmsman looks the name up without regard to case, and refuses names that
lead out of the folder. Each `TRACK n MODE` line starts a track in the
current file. Its `INDEX 01 mm:ss:ff` line says where the track starts in
that file, counted in sectors, at 75 sectors to a second. A track ends where
the next track in the same file starts, or else at the end of the file.

The modes say how large a sector is:

| Mode | Stride | Data starts at |
|---|---|---|
| `MODE1/2048` | 2048 | 0 |
| `MODE1/2352`, `MODE2/2352` | 2352 | 16 or 24 (from the sector's mode byte) |
| `MODE2/2336` | 2336 | 8 |
| `CDG` | 2448 | 0 (music, with 96 bytes of extra data after each sector) |
| `AUDIO` | 2352 | 0 (music) |

`BINARY` files keep their music low byte first, and `MOTOROLA` files keep it
high byte first. `WAVE`, `MP3`, `OGG`, `VORBIS` and `FLAC` files, and any file named
`.wav`, `.ogg`, `.flac` or `.mp3`, are music files. Their first bytes tell
which kind each really is (`RIFF`, `OggS`, `fLaC`, otherwise MP3). They
are decoded to CD samples, and resampled when they are not at 44100 Hz.

The `.gog` and `.ins` files of digital-store releases are cue sheets under
another name.

## A single `.bin`, `.iso` or `.img`

One data track that fills the whole file. Whether its sectors are raw or
cooked is found from the sync bytes, as above.

## CloneCD (`.ccd` with `.img`, and maybe `.sub`)

The `.ccd` is a text file made of `[sections]` and `key=value` lines. Each
`[TRACK n]` section has `MODE=` (0 is music, 1 and 2 are data) and
`INDEX 1=` (the start, in sectors). All tracks are in the `.img` file of the
same name, as raw 2352-byte sectors. The `.sub` file is not needed. An
`.img` that has a `.ccd` beside it is read through that `.ccd`.

## Alcohol (`.mds` with `.mdf`)

The `.mds` is binary, with little-endian numbers. Helmsman reads version 1.

- It begins with `MEDIA DESCRIPTOR`, then the version byte.
- The word at `0x14` is the number of sessions.
- The 32-bit number at `0x50` is where the session list starts.
- Each session takes 24 bytes. Byte 10 holds how many track blocks it has,
  and the 32-bit number at byte 20 is where they start.

Each track block is 80 bytes:

| Byte | Meaning |
|---|---|
| 0 | the mode (low four bits 9 means music) |
| 4 | the track number (values above 99 describe the lead-in and are skipped) |
| 12 | where this track's extra block is (32 bits) |
| 16 | the sector size (16 bits) |
| 40 | where the track starts in the `.mdf` (64 bits) |
| 48 | how many file names there are (32 bits) |
| 52 | where the file name block is (32 bits) |

The extra block holds the pregap, then the length in sectors (32 bits each).
The file name block points at the name and says whether it is one or two
bytes a character. A name of `*.mdf` means the `.mdf` with the same name as
the `.mds`.

## Nero (`.nrg`)

The image data comes first, then a list of chunks, then a footer. All the
numbers are big-endian.

The footer is one of two kinds:

- Newer images end in 12 bytes: `NER5` and a 64-bit offset.
- Older images end in 8 bytes: `NERO` and a 32-bit offset.

That offset is where the chunk list starts. Each chunk is a four-letter name
and a 32-bit length, followed by its body. `END!` ends the list.

### Disc-at-once (`DAOX`, and the older `DAOI`)

A 22-byte header comes first; its first 13 bytes are the catalogue number.
Then there is one entry per track, numbered from 1 in order. An entry is 42
bytes in `DAOX` (64-bit offsets) or 30 bytes in `DAOI` (32-bit offsets):

| Byte | Meaning |
|---|---|
| 0 | the ISRC (12 characters) |
| 12 | the sector size (16 bits) |
| 14 | the mode |
| 18 | where index 0 is in the file |
| 18 + w | where index 1 is (the track's start) |
| 18 + 2w | where the track ends |

Here `w` is 8 for `DAOX` and 4 for `DAOI`.

### Track-at-once (`ETN2`, and the older `ETNF`)

One entry per track. An `ETN2` entry is 32 bytes: a 64-bit start, a 64-bit
length, and the mode in the last byte of the next four. An `ETNF` entry is
20 bytes, the same with 32-bit numbers. The mode alone gives the sector
size, as in the table below.

### Modes

| Mode | Meaning | Sector size |
|---|---|---|
| `00`, `02` | data | 2048 |
| `03` | data, mode 2 | 2336 |
| `05`, `06` | data, whole sectors | 2352 |
| `07` | music | 2352 |
| `0F`, `11` | data, whole sectors with extra data | 2448 |
| `10` | music with extra data | 2448 |

## CHD (`.chd`)

A compressed image, read with libchdr. The image is cut into *hunks* of a
fixed size. Helmsman asks libchdr for one hunk at a time and keeps the last
one it read.

For CDs, each sector takes 2448 bytes in a hunk: 2352 bytes of the sector,
then 96 bytes of extra data. Music samples are high byte first. The tracks
are described in the image's metadata, as text such as
`TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:1234 PREGAP:150 PGTYPE:V...`.

The tracks follow one another in the hunks. Each track's length is rounded
up to a multiple of 4 sectors. When the pregap's type begins with `V`, the
pregap's sectors are kept in the image ahead of the track, so they are
skipped.

## Zip archives (`.zip`, `.zip.001`)

A zip archive ends with a directory of its files.

- **End record.** The last 22 bytes, followed by a comment of up to 64 KiB,
  begin with `PK\5\6`. They give the number of entries, the directory's
  size, and where the directory starts.
- **Large archives.** An archive over 4 GiB, or with many files, also has a
  64-bit end record. A 20-byte locator (`PK\6\7`) just before the end
  record points at it.
- **Directory entries.** Each entry (`PK\1\2`) has these fields:

  | Byte | Meaning |
  |---|---|
  | 8 | flags (bit 0 means a password) |
  | 10 | packing method (0 plain, 8 deflate) |
  | 16 | CRC-32 |
  | 20 | packed size |
  | 24 | size |
  | 28 | the name's length |
  | 30 | the extra data's length |
  | 32 | the comment's length |
  | 42 | where the file's own header is |
  | 46 | the name |

  Where a size or offset holds all ones, the real 64-bit value is in extra
  field 1. That field holds the size, the packed size and the offset, in
  that order, and only the ones that need it.
- **File headers.** Each file's own header (`PK\3\4`) is 30 bytes, then the
  name and the extra data, then the packed bytes.

Helmsman unpacks every file into a folder of its own under its settings
folder (`cache/`), checks each file's CRC, and then opens the image found
there. Names are kept as folders below that one; a name with `..`, a drive
or a root is left out. The folder's name comes from the archive's path,
time and size, so the archive is not unpacked twice. A split archive's
parts (`.001`, `.002`, ...) are read one after another as one archive.
