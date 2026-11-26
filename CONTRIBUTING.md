# Contributing to Helmsman

## Building

You need CMake 3.22 or newer, a C++20 compiler (GCC 11+, Clang 14+ or
Visual Studio 2022) and Ninja or another generator. SDL3 is fetched and
built along with Helmsman.

```
cmake -S . -B build -G Ninja
cmake --build build
./build/tests/hlm_tests
./build/src/app/Helmsman
```

Builds are optimised (`RelWithDebInfo`) unless you choose another build
type. Turn on `-DHLM_WARNINGS_AS_ERRORS=ON` before sending a change; the
checks on GitHub do.

## The games' files

Put your own copies of the games in `gamedata/` (see its README). Never
commit them, or anything taken from them: no pictures, sounds, texts,
tables or screenshots. Tests that need a game skip when it is not there.

## Clean-room rules

Helmsman is written from what the games' files and behaviour show, and
nothing else:

- Do not copy, paste or translate code from other projects that play
  these games, or from disassembled game code. Work out what a thing does,
  describe it in our own words in `docs/`, and write the code from that
  description.
- Every format and rule Helmsman knows is described in `docs/formats/` or
  `docs/rules/`. A change that teaches Helmsman something new updates
  those notes in the same pull request.
- Do not use the games' names or their makers' trademarks as our name, in
  the program, in package names or in file names. Saying in plain text
  which games Helmsman plays is fine.

## Reading outside files

Everything Helmsman reads from the player's files is untrusted. Follow
[SECURITY.md](SECURITY.md): check every length and offset before using it.

## Style

- Code says what it does in plain words. Comments explain why, and the
  game's rules, in short sentences.
- Formatting follows `.clang-format` where there is one; otherwise match
  the code around your change.
- A change a player notices updates `README.md` in the same pull request.
  Build and developer changes belong here or in `docs/`.

## Checking the games

`helmsman-dump` (built in `build/tools/`) helps:

```
helmsman-dump GAME list                  # the archive's files
helmsman-dump GAME events DEMON 1        # every event of a room
helmsman-dump GAME explore LOVE 0 4      # look for a way through a mission
```

`GAME` is a CD image or folder. See the comment at the top of
`tools/dump.cpp` for every command.
