# Security

Helmsman reads files it did not make: CD images, the games' archives,
pictures, sounds, room code and saved games. Any of them may be damaged
or made to cause harm, so all of them are treated as untrusted.

## Rules for code that reads files

- Read through `Reader` (`data/reader.hpp`) or another bounds-checked
  reader. Never index into input without checking the length first.
- Check every size, count and offset against the data before using it,
  and cap sizes so that a bad header cannot make Helmsman allocate without
  limit.
- On bad input, throw `FormatError`. The game then reports the file as
  damaged; it never reads or writes outside its buffers.
- The rooms' x86 code runs only in Helmsman's interpreter, inside a
  memory of its own. It can only reach the game through the room functions
  Helmsman implements, and an instruction the interpreter does not know
  stops that code.
- Helmsman writes only to its own settings and saves folder.

## Reporting a problem

Please report security problems privately, through GitHub's "Report a
vulnerability" on the repository's Security tab, rather than in a public
issue.
