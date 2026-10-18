#!/usr/bin/env python3
"""Deterministic payload for the test corpus.

The corpus scripts in corpus/ install these files. The payload is the test
oracle: run_tests.py compares what the plugin extracts with these bytes.

Usage: make_payload.py <output folder>   (writes <output folder>/payload/...)
"""

import calendar
import hashlib
import os
import sys

# 2021-03-04 05:06:07 UTC; file i gets BASE_MTIME + i, so both odd and even
# seconds occur (Inno Setup rounds times to 2 seconds by default).
BASE_MTIME = 1614834367

# Fixed times (UTC, odd seconds) for the timestamp variants: standard time and
# daylight saving time in New York.
MTIMES = {
    "time/winter.txt": calendar.timegm((2021, 1, 15, 12, 34, 57)),
    "time/summer.txt": calendar.timegm((2021, 7, 15, 12, 34, 59)),
}

# Size of the blocks of Inno Setup's call instruction filter (5.2.0 and later).
FILTER_BLOCK_SIZE = 0x10000


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


def x86_executable(size):
    """Returns a file that Inno Setup takes for an x86 executable ("MZ", e_lfanew,
    "PE\\0\\0", machine i386), so it stores it with the call instruction filter.

    The body is pseudo-random with many E8 / E9 (call / jmp) instructions whose
    address ends with 00 or FF (the filter converts these), instructions inside
    the address of a previous one, an instruction starting 5 to 1 bytes before
    the end of each 64 KiB filter block (only the first is converted), and an
    incomplete instruction at the end of the file.
    """
    data = bytearray(pseudo_random("calls.exe", size))
    choices = pseudo_random("calls.exe instructions", size)
    data[0:0x48] = bytes(0x48)
    data[0:2] = b"MZ"
    data[0x3C:0x40] = (0x40).to_bytes(4, "little")
    data[0x40:0x46] = b"PE\0\0\x4c\x01"
    pos = 0x48
    while pos + 16 < size:
        c = choices[pos]
        data[pos] = 0xE8 if c & 1 else 0xE9
        data[pos + 4] = (0x00, 0xFF, 0x00, 0xFF, 0x12, 0xE8, 0xFF, 0x00)[(c >> 1) & 7]
        if c & 0x30 == 0:
            data[pos + 2] = 0xE8  # inside the address, not an instruction
        pos += 5 + (c >> 2) % 40
    # Each special instruction follows 4 bytes that are not an instruction, so
    # it is not part of the address of an earlier one.
    nops = b"\x90" * 4
    for block in range(size // FILTER_BLOCK_SIZE):
        start = (block + 1) * FILTER_BLOCK_SIZE - 5 + block % 5
        data[start - 4:start + 9] = nops + b"\xe8\x10\x20\x30\x00" + nops
    data[size - 13:size] = nops + b"\xe8\x01\x02\x83\xff\xe9\x00\x00\x00"
    return bytes(data)


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
        ("bin/calls.exe", x86_executable(5 * FILTER_BLOCK_SIZE + 1234)),
        # Names in Windows-1252, for ANSI and Unicode installers.
        ("latin1/na\u00efve caf\u00e9.txt", "Latin-1: na\u00efve caf\u00e9\n".encode("utf-8")),
        ("latin1/Gr\u00f6\u00dfe/\u00dcbung.txt", "Latin-1: Gr\u00f6\u00dfe \u00dcbung\n".encode("utf-8")),
        # Names outside Windows-1252, for Unicode installers only.
        ("unicode/\u041f\u0430\u043f\u043a\u0430/\u0444\u0430\u0439\u043b.txt", b"Cyrillic\n"),
        ("unicode/\u6587\u4ef6\u5939/\u6570\u636e.txt", b"CJK\n"),
        ("unicode/\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac.txt", b"Greek\n"),
        ("unicode/emoji \U0001f600.txt", b"Outside the Basic Multilingual Plane\n"),
        # Entries with the same destination (corpus/common.iss); each has its
        # own content.
    ] + [("collide/%s" % name, ("collide/%s\n" % name).encode("ascii")) for name in COLLIDE_FILES] + [
        ("time/winter.txt", b"Modified in standard time.\n"),
        ("time/summer.txt", b"Modified in daylight saving time.\n"),
    ]


COLLIDE_FILES = [
    "arch-x86.dll", "arch-x64.dll",
    "comp-main.dll", "comp-extra.dll", "comp-none.dll",
    "three-1.dll", "three-2.dll", "three-3.dll",
    "win-x64.dll", "win-x86.dll", "win-arm64.dll",
    "flags-32.dll", "flags-64.dll",
    "lang-en.txt", "lang-de.txt",
    "mixed-1.dll", "mixed-2.dll", "mixed-3.dll", "mixed-4.dll",
    "unique-x64.dll",
]


def mtime(index, rel):
    return MTIMES.get(rel, BASE_MTIME + index)


def mtimes():
    """Returns {relative path: modification time (seconds since 1970, UTC)}."""
    return {rel: mtime(i, rel) for i, (rel, _) in enumerate(files())}


def write(folder):
    """Writes the payload to <folder>/payload and returns the list from files()."""
    payload = files()
    for i, (rel, data) in enumerate(payload):
        path = os.path.join(folder, "payload", *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        os.utime(path, (mtime(i, rel), mtime(i, rel)))
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
