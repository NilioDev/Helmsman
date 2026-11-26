# The library computer and the captain's log (second game)

## The library computer

Spock's "Computer" button (key C) and room function 0x21 open it. It asks
for a topic ("Computer (Select Topic):") until the answer is empty.

The topics are in `DB.KEY` in the `DATA` archive: one line per entry, the
entry's file name first (up to 8 characters, `001`), then the words it is
found by, separated by spaces. A `$` ends the list.

A topic is found when, upper-cased and with a space before it, it appears
in an entry's words (also upper-cased, from the first space of the line
on). So a word's beginning is enough: "balk" finds "Balkos III". Topics of
two letters or fewer find nothing.

Each entry is a text file `NAME.DB`. Its first line is the spoken line's
tag and the title (`#001#Acolytes`); the rest is the text, in lines of
about 60 characters, an empty line between paragraphs, and `$` at the end.
The spoken lines are in `COMPUTER.VCC`, named by the tag.

When nothing is found the computer says "No references for specified
topic." When more than one entry is found, Helmsman lets the player choose
by title (at most eight).

## Typing

Typed answers (the computer, and the rooms' own questions) can be typed on
the keyboard, or picked letter by letter from keys on the screen with the
mouse or the gamepad. A right click takes the last letter back; Escape
gives an empty answer.

## The captain's log

Kirk's "Captain's Log" button (key R) lists the episodes done, the latest
first, each with the points the admiral gave it (see the rating in
[sequel-scripts.md](sequel-scripts.md)), and their sum. The episodes' long
names are in the program's data at 0x263A (a pair of places per episode:
the long name, then the next episode's short name). Before any episode is
done it says "No recent missions have been completed."
