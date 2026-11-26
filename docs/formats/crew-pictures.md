# Crew pictures stored as differences (`.XOR`)

The first game stores most standing and beaming pictures of the crew only
once: the doctor's picture is kept whole, and Kirk's, Spock's and the
security officer's pictures are kept as a difference from it.

A picture named like `kstndn` (Kirk standing, facing north) or a `*tele*`
picture for Kirk (`k`), Spock (`s`) or the security officer (`r`) is
built when `NAME.XOR` exists:

1. Take the doctor's picture: the same name with `m` as its first letter.
2. For Kirk, add 8 to every pixel from colour 0xA8 to 0xAF (the uniform's
   colours). For the security officer, subtract 8. Spock's stay as they
   are.
3. Read the `.XOR` file: a handle x and y (signed 16-bit), a width and a
   height (16-bit), then width x height bytes.
4. Line the difference up by the two handles, and XOR each of its bytes
   into the picture. Bytes that fall outside the picture are skipped.
