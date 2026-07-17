# testd - Test daemon

This is an example user program that connects a proxy device for an
upper MTD device.

It is called `testd` because it demonstrates userspace "intervention" in
I/O requests between the upper MTD device to the backing MTD device,
without any actual meaningful modification to the actual data in between
as intended by other implementations.

It can be used as a reference implementation for creating their a
policy-based MTD transformation utility - it's not intended for actual
real-world usage.

# Manual page

I wrote the manual page in Markdown because it's far the saner option
than groff.

If you still need it in groff format for some (odd) reason, convert with
pandoc:
```sh
pandoc -s -t man testd.md -o testd.1

# Then view -
groff -Tutf8 -man testd.1 | less
```