# Tests

Besides `utests` tests' suite which help finding possible regressions,
here's a table of functional tests to perform before we know this driver
is actually "stable":

| Test Name                                          | What should be checked                               | Condition to pass                                       |
| :------------------------------------------------- | :--------------------------------------------------- | :------------------------------------------------------ |
| Timeout - no userspace running & processing        | Kernel watchdog thread stops I/O transaction         | Got ETIMEDOUT error from MTD ioctl                      |
| Timeout - userspace running & but delay processing | Kernel watchdog thread stops I/O transaction         | Got ETIMEDOUT error from MTD ioctl                      |
| User-mode Passthrough mode                         | User passthrough I/O as-is                           | No errors, read and write to MTD device are transparent |
| User-mode Erroneous processing mode                | User introducing I/O errors (bad ECC, etc)           | Upper layers complaining about bad blocks, bad ECC, etc |
| User-mode BCH-8 processing mode                    | User doing ECC on BCH-8 already-"corrected" data     | I/O transparent to upper layers                         |
| User-mode BCH-16 processing mode                   | User doing ECC on BCH-16 already-"corrected" data    | I/O transparent to upper layers                         |
| User-mode Hamming processing mode                  | User doing ECC on Hamming already-"corrected" data   | I/O transparent to upper layers                         |
| User-mode LDPC processing mode                     | User doing ECC on LDPC already-"corrected" data      | I/O transparent to upper layers                         |
| User-mode encountering bad block                   | User doing I/O on known bad block                    | I/O error propagated to upper layers                    |
| Attaching to NAND chip that requires scrambling?   | Define nandsim with chip that requires scrambling    | Kernel module refues to complete load sequence          |
