#!/usr/bin/env python3
"""Deterministic payload for the test corpus.

The corpus scripts in corpus/ install these files. The payload is the test
oracle: run_tests.py compares what the plugin extracts with these bytes.

Usage: make_payload.py <output folder>   (writes <output folder>/payload/...)
"""

import hashlib
import os
import sys

# 2021-03-04 05:06:07 UTC; file i gets BASE_MTIME + i, so both odd and even
# seconds occur (Inno Setup rounds times to 2 seconds by default). Timestamps
# are not checked yet.
BASE_MTIME = 1614834367


def pseudo_random(seed, size):
    """Returns size bytes from SHA-256 in counter mode (same on every platform)."""
    out = bytearray()
    counter = 0
    seed = seed.encode("ascii")
    while len(out) < size:
        out += hashlib.sha256(seed + counter.to_bytes(8, "little")).digest()
        counter += 1
    return bytes(out[:size])


def text_lines(prefix, count):
    return "".join(
        "%s line %05d: The quick brown fox jumps over the lazy dog.\n" % (prefix, i)
        for i in range(count)
    ).encode("ascii")


def files():
    """Returns the payload as a list of (relative path with '/', bytes)."""
    return [
        ("readme.txt", b"Inno Setup plugin test payload.\n"),
        ("empty.dat", b""),
        ("docs/manual.txt", text_lines("manual", 4000)),
        ("data/random.bin", pseudo_random("random.bin", 3 * 1024 * 1024 + 123)),
        ("data/nested/deeper/leaf.txt", b"A file in nested folders.\n"),
        ("shared.txt", b"This file is installed to two destinations.\n"),
        ("code.txt", b"This file is installed to {code:GetDestDir}.\n"),
    ]


def write(folder):
    """Writes the payload to <folder>/payload and returns the list from files()."""
    payload = files()
    for i, (rel, data) in enumerate(payload):
        path = os.path.join(folder, "payload", *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        os.utime(path, (BASE_MTIME + i, BASE_MTIME + i))
    return payload


def differences(folder):
    """Returns the payload files in <folder>/payload that are missing or differ."""
    bad = []
    for rel, data in files():
        path = os.path.join(folder, "payload", *rel.split("/"))
        try:
            with open(path, "rb") as f:
                if f.read() != data:
                    bad.append(rel)
        except OSError:
            bad.append(rel)
    return bad


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    write(sys.argv[1])
