% nopd(1) Version 1.0
% Liav A
% July 2026

# NAME

ufedm-testd - test ufedm kernel module

# SYNOPSIS

`ufedm-testd` [OPTIONS] DEVICE

# DESCRIPTION

Tests ufedm kernel module - has a SECDEC Hamming code ECC engine to
demonstate a simple ECC engine in use, as a reference implementation
for future implementations.

# OPTIONS

`-h`, `--help`
: Show help.

`-v`, `--verbose`
: Enable verbose output.

`-t`, `--timeout`
: Don't answer with ioctls at all.

`-E`, `--exit-on-error`
: Exit on first error.

`-N`, `--nack-reads`
: Answer with NACKs with a specific errno for read requests.

`-W`, `--nack-writes`
: Answer with NACKs with a specific errno for write requests.


# EXIT STATUS

0
: Success.

1
: Error.
