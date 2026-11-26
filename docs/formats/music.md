# Music (`.AD`, `.MT`, `.PC`, `GTL.AD`)

The music files are Extended MIDI: an IFF file holding one or more songs.
Helmsman plays the AdLib versions (`.AD`) on an emulated FM chip (Nuked
OPL3, under `third_party/`).

## Songs

IFF chunks are a 4-byte id, a 32-bit big-endian length, and the data,
padded to an even length.

- The file is either one `FORM` of type `XMID` (one song), or a `CAT `
  whose `FORM`s are the songs.
- In a song's `FORM`, the `TIMB` chunk lists the instruments it uses: a
  16-bit count, then (patch, bank) byte pairs. The `EVNT` chunk holds its
  events.

## Events

The events are MIDI with two differences:

- There are no delta times. Bytes below 0x80 between events are a pause:
  the bytes in a row are added up, in 120ths of a second.
- A note-on (0x9n) is followed by its note, its velocity and then its
  length (a variable-length number, 7 bits a byte, the top bit meaning
  "more"). There is no note-off for it: the note ends by itself.

Control changes the games use beyond the usual ones: 0x72 chooses the
instrument bank for the next program change, 0x74 starts a repeated part
(the value is how often, 0 for ever), 0x75 ends it.

## Instruments (`GTL.AD`)

A list of 6-byte entries: patch, bank, and a 32-bit offset. The list ends
at a patch or bank of 0xFF. At each offset: a 16-bit size (14), a signed
note shift, and 11 bytes of FM register settings for the instrument's two
operators.

Helmsman plays a song on nine FM voices. A new note takes a free voice, or
else the voice that has sounded longest.
