#!/usr/bin/env python3
"""Test runner for the Inno plugin (see README.md in this folder).

Builds the corpus (payload plus installers compiled from corpus/*.iss), then
checks the plugin through the test client and, when available, through the
Windows 7-Zip: list, extract and test every installer, extract a subset and
skip existing files, check methods, times and progress, open damaged copies,
and compare the results with the payload. Prints one line per check and a
summary; the exit code is 0 only if nothing failed.
"""

import argparse
import collections
import functools
import hashlib
import json
import lzma
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
PLUGIN_DIR = os.path.dirname(TESTS_DIR)
SCRIPTS_DIR = os.path.join(TESTS_DIR, "corpus")
BUILD_DIR = os.path.join(PLUGIN_DIR, "build")
IS_WINDOWS = os.name == "nt"

sys.path.insert(0, TESTS_DIR)
import make_payload  # noqa: E402

COMPILERS = ["5.6.1", "5.6.1-unicode", "6.0.5", "6.1.2", "6.2.2", "6.3.3", "6.4.3", "6.5.4",
             "6.6.1", "6.7.3", "7.0.2-x64", "7.1.0-x64", "7.1.0-x86"]
# Compilers without the preprocessor, unpacked from the installers in
# old-versions.txt: the runner writes a plain script for them (plain_script).
OLD_COMPILERS = ["1.3.26", "2.0.19", "3.0.7", "4.0.8", "4.0.11", "4.1.5", "4.1.8", "4.2.7", "5.1.14",
                 "5.2.3", "5.3.8", "5.3.8-unicode", "5.5.9", "5.5.9-unicode"]
# Script features that old compilers lack -> the first version that has them,
# as far as the probed compilers (1.3.26 to 5.5.9) and the release notes show
# (a feature can be older). Older compilers get NO_<feature> defined, so the
# corpus scripts leave it out, and the variants in VARIANT_FEATURES that need
# it are not built.
FEATURES = {
    "COMPRESSION": (2, 0, 19),  # Compression= (1.3 has only CompressLevel, zlib)
    "COMPONENTS": (2, 0, 19),   # [Types], [Components], Components:
    "RECURSE": (2, 0, 19),      # Flags: recursesubdirs
    "SOLID": (4, 0, 8),         # SolidCompression
    "SLICES": (4, 0, 8),        # DiskSliceSize (older versions span with DiskSize)
    "CODE": (4, 0, 8),          # [Code], {code:...}
    "LANGUAGES": (4, 0, 8),     # [Languages], Languages:
    "UTC": (4, 0, 11),          # TimeStampsInUTC
    "LZMA": (4, 1, 5),          # Compression=lzma
    "ROUNDING": (4, 1, 5),      # TimeStampRounding
    "GERMAN": (4, 2, 7),        # compiler:Languages\German.isl
    "ENCRYPTION": (4, 2, 7),    # Encryption (with ISCrypt.dll)
    "CHECK": (5, 1, 14),        # Check: Is64BitInstallMode, IsWin64, FileExists(); Flags: 32bit, 64bit
    "LZMA2": (5, 3, 9),         # Compression=lzma2
    "ARM64": (5, 6, 0),         # Check: IsARM64
}
# Corpus variants; the scripts set the compression method (see Expected).
VARIANTS = ("lzma-solid", "lzma2-nonsolid", "zip-solid", "zip-nonsolid", "bzip-solid", "bzip-nonsolid",
            "none", "time-utc", "time-local", "spanned")
# Variants -> the FEATURES they need (the timestamp variants need TimeStampRounding=0).
VARIANT_FEATURES = {
    "lzma-solid": ("LZMA", "SOLID"),
    "lzma2-nonsolid": ("LZMA2",),
    "zip-solid": ("SOLID",),
    "bzip-solid": ("COMPRESSION", "SOLID"),
    "bzip-nonsolid": ("COMPRESSION",),
    "none": ("COMPRESSION",),
    "time-utc": ("UTC", "ROUNDING"),
    "time-local": ("ROUNDING",),
}
# Compression directive values -> the method that kpidMethod names.
METHODS = {"zip": "Deflate", "bzip": "BZip2", "lzma": "LZMA", "lzma2": "LZMA2", "none": "Copy"}
# Disk-spanning variants: their data is in slice files next to the installer.
SPANNED_VARIANTS = {"spanned"}
# Encrypted variants -> first Inno Setup version that can compile them. They
# are tested separately (test_encryption) because they need the password.
# Before 6.4, Encryption=yes uses ARC4 from ISCrypt.dll, which the compiler
# needs in its folder; without it the variant is skipped.
ENCRYPTED_VARIANTS = {"encrypted": FEATURES["ENCRYPTION"], "encrypted-full": (6, 5, 0)}
PASSWORD = "inno-test-password"
WRONG_PASSWORD = "wrong-password"
# Timestamp variants -> True if they store times in UTC (TimeStampsInUTC=yes).
TIME_VARIANTS = {"time-utc": True, "time-local": False}
# On Linux, the client lists the timestamp variants in this time zone.
TIME_ZONE = "America/New_York"
# Payload files that are x86 executables. Inno Setup 4.1.8 and later store
# them with a call instruction filter when they are compressed (filter_name).
EXECUTABLES = {"bin/calls.exe"}
TIMEOUT = 600
# Old Inno Setup installers, tested as ready-made installers (test_*_old).
OLD_LIST = os.path.join(TESTS_DIR, "old-versions.txt")
# Known plugin problems (README.md, Known limitations), tested as expected
# failures: a check reports XFAIL while the problem shows, and fails once it
# no longer does, so that this table and the README are updated.
KNOWN_ISSUES = {
    "uninstaller": "before Inno Setup 5.0 the uninstaller is an item with an empty path",
    "codepage": "ANSI installers whose first language uses a code page other than 1252: "
                "non-ASCII text becomes '_', with an archive warning",
    "slices-1.3": "disk spanning before Inno Setup 2.0: the slices <name>.1, <name>.2, ... are not found",
}

# Characters that Windows does not allow in file names. The test client and
# 7-Zip on Windows replace them with '_' in each path component.
INCORRECT_CHARS = re.compile(r'[\x00-\x1f:*?<>|"]')
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
# Extra items whose number depends on the compiler (wizard images).
OPTIONAL_EXTRA = re.compile(r"embedded\\WizardImage(Small|Back)?(Dark)?\d+\.(bmp|png)$")
# The wizard images are named by their content.
IMAGE_MAGIC = {"bmp": b"BM", "png": b"\x89PNG\r\n\x1a\n"}
# Inno Setup [Files] parameters: Name: "value" or Name: value.
PARAMETER = re.compile(r'(\w+)\s*:\s*(?:"([^"]*)"|([^;]*))')
# The client's item results for damaged data.
ERROR_RESULTS = {"crc_error", "data_error", "unexpected_end"}


def fs_components(archive_path):
    """Archive path ('\\'-separated) -> output path components."""
    return [INCORRECT_CHARS.sub("_", c) for c in archive_path.split("\\") if c]


def read_text(path):
    with open(path, "rb") as f:
        # Line endings depend on the checkout (core.autocrlf); hash and parse without them.
        return f.read().replace(b"\r\n", b"\n").decode("ascii")


def filetime(unix_time):
    return (unix_time + 11644473600) * 10 ** 7


class Results:
    def __init__(self):
        self.counts = dict.fromkeys(["PASS", "FAIL", "SKIP", "XFAIL"], 0)

    def report(self, status, name, detail=""):
        self.counts[status] += 1
        print("%-5s %s%s" % (status, name, ": " + detail if detail else ""), flush=True)

    def check(self, name, problems, detail=""):
        if problems:
            self.report("FAIL", name, (detail + "; " if detail else "") + "; ".join(problems[:5])
                        + (" ..." if len(problems) > 5 else ""))
        else:
            self.report("PASS", name, detail)

    def check_known(self, name, problems, symptoms, expected, detail=""):
        """Like check, for an installer with the known issues expected (see
        KNOWN_ISSUES). symptoms: {issue: description} of the known issues seen.
        Returns True if the check failed or reported XFAIL."""
        problems = list(problems)
        problems += [symptoms[issue] for issue in sorted(set(symptoms) - set(expected))]
        problems += ["known issue %s no longer occurs: update KNOWN_ISSUES and README.md" % issue
                     for issue in sorted(set(expected) - set(symptoms))]
        if problems or not symptoms:
            self.check(name, problems, detail)
            return bool(problems)
        self.report("XFAIL", name, "; ".join("known issue %s: %s" % (issue, symptoms[issue])
                                             for issue in sorted(symptoms)))
        return True

    def failed(self):
        return self.counts["FAIL"] > 0

    def summary(self):
        return ", ".join("%d %s" % (n, k) for k, n in self.counts.items())


def run(cmd, cwd=None, env=None):
    """Runs a command and returns (exit code, stdout, stderr); exit code None on timeout."""
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           stdin=subprocess.DEVNULL, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, "", "timeout after %d s" % TIMEOUT
    except OSError as e:
        return None, "", str(e)
    return (p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"))


def exit_problem(code, err, expected=0):
    if code == expected:
        return []
    if code is None:
        return [err.strip() or "cannot run"]
    return ["exit code %d (expected %d)%s" % (code, expected, ": " + err.strip()[-300:] if err.strip() else "")]


# ---------------------------------------------------------------------------
# Corpus


def script_files(variant):
    """The variant script and the files it includes."""
    files = []

    def add(path):
        files.append(path)
        for line in read_text(path).split("\n"):
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                add(os.path.join(os.path.dirname(path), m.group(1)))

    add(os.path.join(SCRIPTS_DIR, variant + ".iss"))
    return files


def script_lines(path, defined):
    """Yields the lines of a corpus script with #include files inserted,
    #ifdef, #ifndef, #else and #endif applied and {#name} replaced outside
    comments. defined: {symbol: value} (#define adds to it)."""
    active = []
    for line in read_text(path).split("\n"):
        s = line.strip()
        if not s.startswith("#"):
            if all(active):
                if not s.startswith(";"):
                    line = re.sub(r"\{#(\w+)\}", lambda m: defined[m.group(1)], line)
                yield line
            continue
        words = s[1:].split()
        directive = words[0] if words else ""
        if directive in ("ifdef", "ifndef") and len(words) == 2:
            active.append((words[1] in defined) == (directive == "ifdef"))
        elif directive == "else" and active:
            active[-1] = not active[-1]
        elif directive == "endif" and active:
            active.pop()
        elif not all(active):
            continue
        elif directive == "include":
            m = re.match(r'#\s*include\s+"([^"]+)"$', s)
            if not m:
                raise ValueError("%s: unsupported #include: %s" % (path, s))
            yield from script_lines(os.path.join(os.path.dirname(path), m.group(1)), defined)
        elif directive == "define" and len(words) >= 2:
            m = re.match(r'#\s*define\s+\w+\s*(?:"([^"]*)")?$', s)
            if not m:
                raise ValueError("%s: unsupported #define: %s" % (path, s))
            defined[words[1]] = m.group(1) or ""
        else:
            raise ValueError("%s: unsupported preprocessor line: %s" % (path, s))
    if active:
        raise ValueError("%s: #ifdef without #endif" % path)


def is_unicode(compiler):
    """Unicode compilers: the -unicode builds of Inno Setup 5, and 6 and later."""
    return compiler.endswith("-unicode") or compiler_version(compiler) >= (6, 0, 0)


def script_symbols(compiler):
    """The preprocessor symbols of the corpus scripts for a compiler: UNICODE,
    and for the old compilers SMALL_CORPUS and NO_<feature> (FEATURES)."""
    defined = {"UNICODE": ""} if is_unicode(compiler) else {}
    if compiler in OLD_COMPILERS:
        defined["SMALL_CORPUS"] = ""
        defined.update(("NO_" + f, "") for f, first in FEATURES.items() if compiler_version(compiler) < first)
    return defined


def filter_name(compiler):
    """The call instruction filter of compressed x86 executables, or None."""
    version = compiler_version(compiler)
    if version < (4, 1, 8):
        return None
    return "IF4108" if version < (5, 2, 0) else "IF5200" if version < (5, 3, 9) else "IF5309"


class Expected:
    """What a corpus installer contains, from its scripts (see corpus/common.iss).

    entries: [(item path, payload path with '/')] for the [Files] entries;
    sources: the same as a dict; extras: the embedded items that must be
    listed (the wizard images are optional, their number depends on the
    compiler); method and filter: the compression method and the call
    instruction filter (or None) as kpidMethod names them."""

    def __init__(self, compiler, variant):
        tag = "%s/%s" % (compiler, variant)
        payload_names = [rel for rel, _ in make_payload.files()]
        self.entries, languages = [], []
        # Without a Compression directive (Inno Setup 1.3), files are zlib-compressed.
        compression = "zip"
        section, expect = None, None
        for line in script_lines(os.path.join(SCRIPTS_DIR, variant + ".iss"), script_symbols(compiler)):
            s = line.strip()
            m = re.match(r";\s*expect:\s*(.*\S)", s)
            if m:
                expect = m.group(1)
                continue
            if not s or s.startswith(";"):
                continue
            if s.startswith("["):
                section = s.lower()
                continue
            if section == "[setup]" and s.lower().startswith("compression="):
                compression = s.split("=", 1)[1].split("/")[0].strip().lower()
            if section not in ("[files]", "[languages]"):
                continue
            params = {}
            for p in PARAMETER.finditer(s):
                params[p.group(1).lower()] = p.group(2) if p.group(2) is not None else p.group(3).strip()
            if section == "[languages]":
                languages.append(params["name"])
                continue
            source = params.get("source", "")
            if not source.startswith("payload\\") or "destdir" not in params:
                raise ValueError("%s: unsupported [Files] entry: %s" % (tag, s))
            rel = source[len("payload\\"):].replace("\\", "/")
            dest_dir = params["destdir"].rstrip("\\")
            if rel.endswith("/*"):
                # The payload files in that folder, with recursesubdirs also in its subfolders.
                prefix = rel[:-1]
                recurse = "recursesubdirs" in params.get("flags", "").lower().split()
                matched = [n for n in payload_names
                           if n.startswith(prefix) and (recurse or "/" not in n[len(prefix):])]
                if expect or not matched:
                    raise ValueError("%s: unsupported [Files] entry: %s" % (tag, s))
                self.entries += [(dest_dir + "\\" + n[len(prefix):].replace("/", "\\"), n) for n in matched]
                continue
            if rel not in payload_names:
                raise ValueError("%s: not a payload file: %s" % (tag, source))
            self.entries.append((expect or dest_dir + "\\" + params.get("destname", rel.split("/")[-1]), rel))
            expect = None
        if expect:
            raise ValueError("%s: '; expect:' without an entry" % tag)
        seen = set()
        for path, _ in self.entries:
            if path.lower() in seen:
                raise ValueError("%s: two entries for %s (add '; expect:' lines)" % (tag, path))
            seen.add(path.lower())
        self.sources = dict(self.entries)
        # Without a [Languages] section, Inno Setup names the language "default";
        # Inno Setup 1.3 stores no languages.
        if not languages and compiler_version(compiler) >= (2, 0, 0):
            languages = ["default"]
        self.extras = ["embedded\\install_script.iss"] + ["embedded\\%s.isl" % name for name in languages]
        # Before Inno Setup 4.2.5 every chunk is compressed: Compression=none
        # gives zlib streams of stored blocks.
        if compression == "none" and compiler_version(compiler) < (4, 2, 5):
            compression = "zip"
        self.method = METHODS[compression]
        self.filter = None if compression == "none" else filter_name(compiler)


def plain_script(compiler, variant):
    """The script of a variant for an old compiler (no preprocessor): the
    preprocessor lines applied, and in [Setup] the folders (the script is in
    the output folder installers/<compiler>), the output name, AppVerName
    (required before Inno Setup 5.5) and before Inno Setup 5.0
    Uninstallable=no (the uninstaller is an item; see KNOWN_ISSUES)."""
    lines = list(script_lines(os.path.join(SCRIPTS_DIR, variant + ".iss"), script_symbols(compiler)))
    app = dict(line.split("=", 1) for line in lines if re.match(r"App(Name|Version)=", line))
    setup = ["SourceDir=..\\..", "OutputDir=installers\\" + compiler, "OutputBaseFilename=" + variant,
             "AppVerName=%s %s" % (app["AppName"], app["AppVersion"])]
    if compiler_version(compiler) < (5, 0, 0):
        setup.append("Uninstallable=no")
    i = lines.index("[Setup]") + 1
    return "\r\n".join(lines[:i] + setup + lines[i:]) + "\r\n"


def corpus_key(compiler, variant):
    h = hashlib.sha256()
    h.update(("%s\n%s\n" % (compiler, variant)).encode("ascii"))
    if compiler in OLD_COMPILERS:
        h.update(plain_script(compiler, variant).encode("ascii"))
    else:
        for path in script_files(variant):
            h.update(read_text(path).encode("ascii"))
    h.update(read_text(os.path.join(TESTS_DIR, "make_payload.py")).encode("ascii"))
    return h.hexdigest()


def find_iscc(compiler):
    return shutil.which("iscc-" + compiler)


def compiler_version(compiler):
    """Compiler label (e.g. "7.1.0-x64") -> (major, minor, patch)."""
    return tuple(int(x) for x in re.match(r"(\d+)\.(\d+)\.(\d+)", compiler).groups())


def missing_iscrypt(compiler, variant):
    """Reason why an ARC4 variant cannot be compiled, or None."""
    if variant not in ENCRYPTED_VARIANTS or compiler_version(compiler) >= (6, 4, 0) or IS_WINDOWS:
        return None
    prefix = os.environ.get("WINEPREFIX") or os.path.expanduser("~/.wine")
    if os.path.isfile(os.path.join(prefix, "drive_c", "iscc", compiler, "ISCrypt.dll")):
        return None
    return "ISCrypt.dll is not installed (ARC4 encryption before Inno Setup 6.4)"


def variants(compiler):
    """The VARIANTS that a compiler can build (VARIANT_FEATURES)."""
    missing = script_symbols(compiler)
    return [v for v in VARIANTS if not any("NO_" + f in missing for f in VARIANT_FEATURES.get(v, ()))]


def encrypted_variants(compiler):
    return [v for v, first in ENCRYPTED_VARIANTS.items() if compiler_version(compiler) >= first]


def installer_issues(compiler, variant):
    """The KNOWN_ISSUES of a corpus installer."""
    if variant in SPANNED_VARIANTS and compiler_version(compiler) < (2, 0, 0):
        return {"slices-1.3"}
    return set()


def slice_files(exe):
    """The slice files of a disk-spanning installer: setup-1.bin, setup-2.bin,
    ... (setup.1, setup.2, ... before Inno Setup 2.0)."""
    folder, name = os.path.split(exe)
    pattern = re.compile(re.escape(os.path.splitext(name)[0]) + r"(?:-(\d+)\.bin|\.(\d+))$")
    found = [(int(m.group(1) or m.group(2)), n) for n in (os.listdir(folder) if os.path.isdir(folder) else [])
             for m in [pattern.match(n)] if m]
    return [os.path.join(folder, n) for _, n in sorted(found)]


def build_corpus(args, results, encrypted=False):
    """Writes the payload, compiles (or reuses) the installers. Returns {(compiler, variant): exe}
    for VARIANTS, or with encrypted=True for ENCRYPTED_VARIANTS."""
    corpus = args.corpus_dir
    installers = {}
    if encrypted:
        pass
    elif args.no_compile:
        bad = make_payload.differences(corpus)
        results.check("payload in %s" % corpus, ["missing or different: " + ", ".join(bad)] if bad else [])
    else:
        os.makedirs(corpus, exist_ok=True)
        make_payload.write(corpus)
        for name in os.listdir(SCRIPTS_DIR):
            if name.endswith(".iss"):
                shutil.copyfile(os.path.join(SCRIPTS_DIR, name), os.path.join(corpus, name))
    for compiler in args.compilers:
        out_dir = os.path.join(corpus, "installers", compiler)
        for variant in (encrypted_variants(compiler) if encrypted else variants(compiler)):
            name = "%s %s/%s" % ("installer" if args.no_compile else "compile", compiler, variant)
            exe = os.path.join(out_dir, variant + ".exe")
            key_file = os.path.join(out_dir, variant + ".key")
            key = corpus_key(compiler, variant)
            cached = os.path.isfile(exe) and os.path.isfile(key_file) and read_text(key_file).strip() == key
            if cached:
                results.report("PASS", name, "prebuilt" if args.no_compile else "cached")
                installers[(compiler, variant)] = exe
                continue
            if args.no_compile:
                if encrypted and compiler_version(compiler) < (6, 4, 0):
                    # Compiled only where ISCrypt.dll was available (see missing_iscrypt).
                    results.report("SKIP", name, "not in the prebuilt corpus (ARC4 needs ISCrypt.dll)")
                else:
                    results.report("FAIL", name, "installer missing or built from other scripts (--no-compile)")
                continue
            if missing_iscrypt(compiler, variant):
                results.report("SKIP", name, missing_iscrypt(compiler, variant))
                continue
            iscc = find_iscc(compiler)
            if not iscc:
                results.report("FAIL", name, "iscc-%s not found" % compiler)
                continue
            os.makedirs(out_dir, exist_ok=True)
            for f in [exe, key_file] + slice_files(exe):
                if os.path.exists(f):
                    os.remove(f)
            if compiler in OLD_COMPILERS:
                script = os.path.join(out_dir, variant + ".iss")
                with open(script, "w", newline="") as f:
                    f.write(plain_script(compiler, variant))
                cmd = [iscc, script]
            else:
                cmd = [iscc, "/Q", "/O" + out_dir, "/F" + variant, os.path.join(corpus, variant + ".iss")]
            # Installers store local times unless TimeStampsInUTC=yes; compile in UTC.
            code, out, err = run(cmd, cwd=corpus, env=dict(os.environ, TZ="UTC"))
            problems = exit_problem(code, (err + out)[-500:])
            if not problems and not os.path.isfile(exe):
                problems.append("no output file%s" % (": " + (err + out).strip()[-300:] if (err + out).strip() else ""))
            if not problems and variant in SPANNED_VARIANTS and len(slice_files(exe)) < 3:
                problems.append("%d slice files, expected at least 3" % len(slice_files(exe)))
            results.check(name, problems)
            if not problems:
                with open(key_file, "w") as f:
                    f.write(key + "\n")
                installers[(compiler, variant)] = exe
    return installers


# Uncompressed size of the setup header block of the "bomb" copies, beyond
# the limit of the plugin (256 MiB).
BOMB_SIZE = 1 << 30
# Kinds of damaged copies that cannot be opened.
DECLINED_KINDS = ("truncated", "headers", "bomb")


@functools.lru_cache(maxsize=None)
def bomb_block_data():
    """LZMA data of BOMB_SIZE zero bytes in 4 KiB pieces, each after its CRC-32."""
    compressor = lzma.LZMACompressor(format=lzma.FORMAT_ALONE, filters=[
        {"id": lzma.FILTER_LZMA1, "preset": 0, "dict_size": 1 << 16}])
    zeros = bytes(1 << 20)
    parts = [compressor.compress(zeros) for _ in range(BOMB_SIZE // len(zeros))]
    alone = b"".join(parts) + compressor.flush()
    # Inno Setup stores the 5 bytes of LZMA properties without the size field.
    raw = alone[:5] + alone[13:]
    return b"".join(struct.pack("<I", zlib.crc32(raw[i:i + 4096])) + raw[i:i + 4096]
                    for i in range(0, len(raw), 4096))


def make_bomb_block(compiler):
    """A setup header block (for the version of compiler) whose LZMA data
    decompresses to BOMB_SIZE zero bytes."""
    stored = bomb_block_data()
    size_format = "<Q" if compiler_version(compiler) >= (6, 7, 0) else "<I"
    header = struct.pack(size_format, len(stored)) + b"\x01"
    return struct.pack("<I", zlib.crc32(header)) + header + stored


def make_damaged(args, results, installers, payload):
    """Writes damaged copies of corpus installers. Returns {(compiler, kind): (path, detail)}.

    Inno Setup writes the setup headers after the file data, so a truncated
    installer cannot be opened at all; flipped bytes in the file data give
    per-item errors:
      stored     none.exe, one bit flipped inside data/random.bin
      solid      lzma-solid.exe, one byte changed in the middle of its chunk
      truncated  lzma-solid.exe cut in the middle of its chunk
      headers    lzma-solid.exe cut inside the setup headers
      bomb       lzma-solid.exe whose first setup header block decompresses
                 to BOMB_SIZE bytes
    """
    damaged = {}
    for compiler in args.compilers:
        if compiler in OLD_COMPILERS:
            continue
        for kind, variant in (("stored", "none"), ("solid", "lzma-solid"),
                              ("truncated", "lzma-solid"), ("headers", "lzma-solid"),
                              ("bomb", "lzma-solid")):
            exe = installers.get((compiler, variant))
            if not exe:
                continue
            with open(exe, "rb") as f:
                data = bytearray(f.read())
            problem = None
            if kind == "bomb":
                headers = data.rfind(b"Inno Setup Setup Data")
                if headers < 0:
                    problem = "setup headers not found"
                else:
                    # The 64-byte version, then the encryption header (6.5+).
                    offset = headers + 64 + (53 if compiler_version(compiler) >= (6, 5, 0) else 0)
                    data[offset:] = make_bomb_block(compiler)
            elif kind == "stored":
                random_bin = payload["data/random.bin"]
                needle = random_bin[len(random_bin) // 2 - 32:len(random_bin) // 2 + 32]
                if data.count(needle) != 1:
                    problem = "random.bin marker occurs %d times (expected once)" % data.count(needle)
                else:
                    offset = data.find(needle) + len(needle) // 2
                    data[offset] ^= 1
            else:
                headers = data.rfind(b"Inno Setup Setup Data")
                chunk = data.rfind(b"zlb\x1a", 0, max(headers, 0))
                if headers < 0 or chunk < 0:
                    problem = "setup headers or chunk not found"
                elif kind == "solid":
                    offset = (chunk + headers) // 2
                    data[offset] ^= 0x55
                elif kind == "truncated":
                    offset = (chunk + headers) // 2
                    del data[offset:]
                else:
                    offset = headers + 200
                    del data[offset:]
            name = "damaged %s/%s-%s" % (compiler, variant, kind)
            if problem:
                results.report("FAIL", name, problem)
                continue
            path = os.path.join(args.work_dir, "damaged", compiler, "%s-%s.exe" % (variant, kind))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            detail = "%s at offset %d" % ("cut" if kind in ("truncated", "headers") else
                                          "header block of %d MiB" % (BOMB_SIZE >> 20) if kind == "bomb" else
                                          "changed byte", offset)
            damaged[(compiler, kind)] = (path, detail)
    return damaged


# ---------------------------------------------------------------------------
# Comparing with the payload


def compare_listing(listed, expected, payload):
    """listed: [(path, size)] as listed. Returns a list of problems."""
    problems = []
    sizes, seen = {}, set()
    for path, size in listed:
        if path.lower() in seen:
            problems.append("duplicate path %s" % path)
        seen.add(path.lower())
        sizes[path] = size
    for path, source in expected.entries:
        if path not in sizes:
            problems.append("missing %s" % path)
        elif sizes[path] != len(payload[source]):
            problems.append("%s: size %s, expected %d" % (path, sizes[path], len(payload[source])))
    for path in expected.extras:
        if path not in sizes:
            problems.append("missing %s" % path)
    for path in sizes:
        if path not in expected.sources and path not in expected.extras and not OPTIONAL_EXTRA.match(path):
            problems.append("unexpected item %s" % path)
    return problems


def compare_folder(out_dir, items, sources, payload, unchecked=()):
    """Compares the files in out_dir with the extracted items ({path: listed
    size}): payload files byte for byte (sources: {path: payload path}), other
    items (embedded files) by size. The files of the items in unchecked may
    differ or be missing. Returns a list of problems."""
    problems = []
    expected = {tuple(fs_components(path)): path for path in items}
    tolerated = {tuple(fs_components(path)) for path in unchecked}
    found = set()
    for root, _, names in os.walk(out_dir):
        for n in names:
            found.add(tuple(os.path.relpath(os.path.join(root, n), out_dir).split(os.sep)))
    for comps, path in sorted(expected.items()):
        if comps in tolerated:
            continue
        if comps not in found:
            problems.append("not extracted: %s" % path)
            continue
        with open(os.path.join(out_dir, *comps), "rb") as f:
            data = f.read()
        if path in sources:
            if data != payload[sources[path]]:
                problems.append("%s: content differs from payload/%s (size %d, expected %d)"
                                % (path, sources[path], len(data), len(payload[sources[path]])))
        elif len(data) != items[path]:
            problems.append("%s: size %d, listed %s" % (path, len(data), items[path]))
        elif OPTIONAL_EXTRA.search(path) and not data.startswith(IMAGE_MAGIC[path.rsplit(".", 1)[1]]):
            problems.append("%s: content is not a %s image" % (path, path.rsplit(".", 1)[1]))
    for comps in sorted(found - set(expected) - tolerated):
        problems.append("unexpected file %s" % "\\".join(comps))
    return problems


def method_problems(expected, archive_method, items):
    """Checks kpidMethod: the method of the variant, plus the call instruction
    filter for compressed executables. items: [(path, method)]."""
    method = expected.method
    filtered = "%s %s" % (method, expected.filter) if expected.filter else method
    has_executable = any(source in EXECUTABLES for source in expected.sources.values())
    problems = []
    if archive_method != (filtered if has_executable else method):
        problems.append("archive method %r, expected %r" % (archive_method, filtered if has_executable else method))
    for path, item_method in items:
        source = expected.sources.get(path)
        want = None if source is None else filtered if source in EXECUTABLES else method
        if (item_method or None) != want:
            problems.append("%s: method %r, expected %r" % (path, item_method, want))
    return problems


def local_to_utc(local_filetime, time_zone=None):
    """Returns what LocalFileTimeToFileTime gives now: it uses the current
    offset of the time zone (time_zone, or the local one), not the one of the
    date. Windows always uses its own time zone."""
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes
        local = wintypes.FILETIME(local_filetime & 0xFFFFFFFF, local_filetime >> 32)
        utc = wintypes.FILETIME()
        if not ctypes.windll.kernel32.LocalFileTimeToFileTime(ctypes.byref(local), ctypes.byref(utc)):
            raise OSError("LocalFileTimeToFileTime failed")
        return utc.dwLowDateTime | (utc.dwHighDateTime << 32)
    import datetime
    if time_zone:
        import zoneinfo
        now = datetime.datetime.now(zoneinfo.ZoneInfo(time_zone))
    else:
        now = datetime.datetime.now().astimezone()
    return local_filetime - int(now.utcoffset().total_seconds()) * 10 ** 7


def expected_mtimes(variant, expected, time_zone=None):
    """Returns {item path: FILETIME} for a timestamp variant: the payload
    times, converted from local time unless the installer stores UTC."""
    mtimes = make_payload.mtimes()
    result = {}
    for path, source in expected.entries:
        stored = filetime(mtimes[source])
        result[path] = stored if TIME_VARIANTS[variant] else local_to_utc(stored, time_zone)
    return result


def mtime_problems(before, after, got):
    """Compares {path: FILETIME} with the times that expected_mtimes() gave
    before and after the run (the time zone offset may change in between)."""
    problems = []
    for path in sorted(before):
        if got.get(path) not in (before[path], after[path]):
            problems.append("%s: mtime %s, expected %d" % (path, got.get(path), before[path]))
    return problems


def modify_output(out_dir, expected):
    """Prepares an extracted folder for the skip-existing check: changes the
    first payload file and deletes one in the middle. Returns (changed path,
    deleted path, new content of the changed file)."""
    changed = expected.entries[0][0]
    deleted = expected.entries[len(expected.entries) // 2][0]
    content = b"Changed after the first extraction.\n"
    with open(os.path.join(out_dir, *fs_components(changed)), "wb") as f:
        f.write(content)
    os.remove(os.path.join(out_dir, *fs_components(deleted)))
    return changed, deleted, content


# ---------------------------------------------------------------------------
# Test client


class Client:
    def __init__(self, args, env):
        self.exe = args.client
        self.plugin = args.plugin
        self.env = env

    def run(self, *cmd):
        """Runs the client. Returns (exit code, JSON objects, problems); the
        problems include output that is not a JSON line (the plugin must be
        silent). Paths are returned with '\\'."""
        code, out, err = run([self.exe, self.plugin] + list(cmd), env=self.env)
        objs, problems = [], []
        if code is None:
            problems.append(err)
        elif err.strip():
            problems.append("stderr: %s" % err.strip()[:300])
        for line in out.splitlines():
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
            except ValueError:
                obj = None
            if not isinstance(obj, dict):
                problems.append("not a JSON line: %r" % line[:200])
                continue
            if isinstance(obj.get("path"), str):
                obj["path"] = obj["path"].replace("/", "\\")
            objs.append(obj)
        return code, objs, problems


def of_type(objs, kind):
    return [o for o in objs if o.get("type") == kind]


def client_exit(code, objs, expected=0):
    if code == expected:
        return []
    errors = "; ".join(o.get("message", "") + (" " + o["name"] if o.get("name") else "")
                       + (" " + o["hresult"] if o.get("hresult") else "") for o in of_type(objs, "error"))
    return ["exit code %s (expected %d)%s" % (code, expected, ": " + errors if errors else "")]


def extract_problems(objs, mode, indices, total=None, skipped=0, errors=0):
    """Checks the item results and the extract_result line of a client t or x.
    indices: the items that must get a result. total: the expected progress
    total, or None for at least the sum of the sizes (a subset also counts the
    data skipped before its items)."""
    problems = []
    results = of_type(objs, "item_result")
    got = sorted(o.get("index") for o in results)
    if got != sorted(indices):
        problems.append("results for items %s, expected %s" % (got[:20], sorted(indices)[:20]))
    for o in results:
        if o.get("mode") != mode:
            problems.append("%s: mode %s" % (o.get("path"), o.get("mode")))
        if errors == 0 and o.get("result") != "ok":
            problems.append("%s: %s" % (o.get("path"), o.get("result")))
    ends = of_type(objs, "extract_result")
    if len(ends) != 1:
        return problems + ["%d extract_result lines" % len(ends)]
    end = ends[0]
    if end.get("hresult") != "0x00000000":
        problems.append("hresult %s" % end.get("hresult"))
    if (end.get("items"), end.get("errors"), end.get("skipped")) != (len(indices), errors, skipped):
        problems.append("items %s, errors %s, skipped %s; expected %d, %d, %d" % (
            end.get("items"), end.get("errors"), end.get("skipped"), len(indices), errors, skipped))
    if end.get("completed_decreased"):
        problems.append("progress went backwards")
    if end.get("completed") != end.get("total"):
        problems.append("progress completed %s of total %s" % (end.get("completed"), end.get("total")))
    if total is not None and end.get("total") != total:
        problems.append("progress total %s, expected the sum of the sizes %d" % (end.get("total"), total))
    return problems


def test_client(args, results, installers, expected, damaged, payload, env):
    for f in (args.client, args.plugin):
        if not os.path.isfile(f):
            results.report("FAIL", "client", "%s not found (build it or pass --client/--plugin)" % f)
            return
    client = Client(args, env)
    code, objs, problems = client.run("i")
    problems += client_exit(code, objs)
    if not any(o.get("name") == "Inno" and o.get("ext") == "exe" for o in of_type(objs, "format")):
        problems.append("no Inno format with extension exe")
    results.check("client i", problems)

    for (compiler, variant), exe in sorted(installers.items()):
        tag = "%s/%s" % (compiler, variant)
        exp = expected[(compiler, variant)]
        time_zone = None if IS_WINDOWS else TIME_ZONE
        before = expected_mtimes(variant, exp, time_zone) if variant in TIME_VARIANTS else None

        code, objs, problems = client.run("l", exe)
        problems += client_exit(code, objs)
        archive = (of_type(objs, "archive") or [{}])[0]
        items = of_type(objs, "item")
        problems += compare_listing([(o.get("path"), o.get("size")) for o in items], exp, payload)
        if archive.get("phy_size") != os.path.getsize(exe):
            problems.append("phy_size %s, file size %d" % (archive.get("phy_size"), os.path.getsize(exe)))
        symptoms = {}
        if "Missing slice" in archive.get("warning", ""):
            symptoms["slices-1.3"] = "archive warning %r" % archive["warning"]
        for key in ("error", "warning"):
            if key in archive and not (key == "warning" and symptoms):
                problems.append("archive %s: %s" % (key, archive[key]))
        issues = installer_issues(compiler, variant)
        # With a known issue the other checks of the installer would fail too.
        if results.check_known("client l %s" % tag, problems, symptoms, issues) and issues:
            continue
        if not items:
            continue
        listed = {o["path"]: o.get("size") for o in items}
        indices = {o["path"]: o["index"] for o in items}
        all_indices = sorted(indices.values())
        sizes = sum(listed.values())

        results.check("client method %s" % tag, method_problems(
            exp, archive.get("method"), [(o["path"], o.get("method")) for o in items]))
        if before:
            problems = mtime_problems(before, expected_mtimes(variant, exp, time_zone),
                                      {o["path"]: o.get("mtime") for o in items})
            results.check("client mtime %s" % tag, problems, "stored in UTC" if TIME_VARIANTS[variant]
                          else "stored in local time, listed in %s" % (time_zone or "the local time zone"))

        out_dir = os.path.join(args.work_dir, "client", compiler, variant)
        code, objs, problems = client.run("x", exe, out_dir)
        problems += client_exit(code, objs)
        problems += extract_problems(objs, "extract", all_indices, sizes)
        problems += compare_folder(out_dir, listed, exp.sources, payload)
        results.check("client x %s" % tag, problems)
        extracted = not problems

        code, objs, problems = client.run("t", exe)
        problems += client_exit(code, objs)
        problems += extract_problems(objs, "test", all_indices, sizes)
        results.check("client t %s" % tag, problems)

        # Every other item, in reverse order: the plugin decodes in data order.
        subset = all_indices[::2][::-1]
        option = "-i" + ",".join(str(i) for i in subset)
        subset_items = {path: size for path, size in listed.items() if indices[path] in subset}
        subset_dir = os.path.join(args.work_dir, "client-subset", compiler, variant)
        code, objs, problems = client.run("x", exe, subset_dir, option)
        problems += client_exit(code, objs)
        problems += extract_problems(objs, "extract", subset)
        problems += compare_folder(subset_dir, subset_items, exp.sources, payload)
        code, objs, test_problems = client.run("t", exe, option)
        problems += test_problems + client_exit(code, objs) + extract_problems(objs, "test", subset)
        results.check("client subset %s" % tag, problems, "%d of %d items, reverse order" % (len(subset), len(listed)))

        # Skip existing files: only the deleted file is extracted again.
        if not extracted:
            results.report("SKIP", "client skip-existing %s" % tag, "extraction failed")
            continue
        changed, deleted, content = modify_output(out_dir, exp)
        code, objs, problems = client.run("x", exe, out_dir, "-s")
        problems += client_exit(code, objs)
        problems += extract_problems(objs, "extract", [indices[deleted]], skipped=len(listed) - 1)
        skipped = sorted(o.get("index") for o in of_type(objs, "skipped"))
        if skipped != sorted(i for path, i in indices.items() if path != deleted):
            problems.append("skipped items %s" % skipped[:20])
        problems += compare_folder(out_dir, listed, exp.sources, payload, unchecked=[changed])
        with open(os.path.join(out_dir, *fs_components(changed)), "rb") as f:
            if f.read() != content:
                problems.append("%s was overwritten" % changed)
        results.check("client skip-existing %s" % tag, problems, "deleted %s" % deleted)

    test_client_damaged(args, results, client, installers, expected, damaged, payload)
    test_client_missing_slice(args, results, client, installers, expected, payload)


def test_client_damaged(args, results, client, installers, expected, damaged, payload):
    for (compiler, kind), (path, detail) in sorted(damaged.items()):
        variant = "none" if kind == "stored" else "lzma-solid"
        exp = expected[(compiler, variant)]
        name = "client damaged %s/%s-%s" % (compiler, variant, kind)
        if kind in DECLINED_KINDS:
            # Never a crash or hang: the installer is declined.
            code, objs, problems = client.run("l", path)
            problems += client_exit(code, objs, expected=2)
            if kind == "bomb" and not IS_WINDOWS:
                # The block fails at the limit instead of being decoded whole.
                import resource
                max_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss * 1024
                if max_rss >= BOMB_SIZE * 3 // 4:
                    problems.append("a child process used %d MiB" % (max_rss >> 20))
            results.check(name, problems, detail + ", cannot open")
            continue
        code, objs, problems = client.run("l", installers[(compiler, variant)])
        listed = {o["path"]: o.get("size") for o in of_type(objs, "item")}
        out_dir = os.path.join(args.work_dir, "client-damaged", compiler, kind)
        code, objs, x_problems = client.run("x", path, out_dir)
        problems += x_problems + client_exit(code, objs, expected=4)
        item_results = of_type(objs, "item_result")
        bad = [o for o in item_results if o.get("result") != "ok"]
        problems += ["%s: %s" % (o.get("path"), o.get("result")) for o in bad
                     if o.get("result") not in ERROR_RESULTS]
        if kind == "stored":
            # Only the changed file is damaged.
            want = sorted(p for p, s in exp.sources.items() if s == "data/random.bin")
            if sorted(o.get("path") for o in bad) != want or any(o.get("result") != "crc_error" for o in bad):
                problems.append("errors %s, expected crc_error for %s" % (
                    ["%s: %s" % (o.get("path"), o.get("result")) for o in bad][:5], want))
        elif not bad:
            problems.append("no item errors")
        ends = of_type(objs, "extract_result")
        if not ends or ends[0].get("hresult") != "0x00000000" or ends[0].get("completed") != ends[0].get("total"):
            problems.append("extract_result %s" % (ends[:1],))
        problems += compare_folder(out_dir, listed, exp.sources, payload, unchecked=[o.get("path") for o in bad])
        ok = sum(o.get("result") == "ok" and o.get("path") in exp.sources for o in item_results)
        results.check(name, problems, "%s, %d files ok, %d errors" % (detail, ok, len(bad)))

    # A PE file that is not an installer is declined.
    exe = os.path.join(args.corpus_dir, "payload", "bin", "calls.exe")
    code, objs, problems = client.run("l", exe)
    problems += client_exit(code, objs, expected=2)
    results.check("client l payload/bin/calls.exe", problems, "not an installer")


def test_client_missing_slice(args, results, client, installers, expected, payload):
    """Disk spanning with a slice missing: the files with data in it or after
    it in the same chunk are unavailable, the others are extracted."""
    for (compiler, variant), exe in sorted(installers.items()):
        if variant not in SPANNED_VARIANTS:
            continue
        name = "client missing slice %s/%s" % (compiler, variant)
        if installer_issues(compiler, variant):
            results.report("SKIP", name, "known issue %s" % ", ".join(sorted(installer_issues(compiler, variant))))
            continue
        slices = slice_files(exe)
        missing = os.path.basename(slices[len(slices) // 2])
        work = os.path.join(args.work_dir, "missing-slice", compiler)
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
        for f in [exe] + slices:
            if os.path.basename(f) != missing:
                shutil.copy2(f, work)
        path = os.path.join(work, os.path.basename(exe))
        exp = expected[(compiler, variant)]
        code, objs, problems = client.run("l", path)
        problems += client_exit(code, objs)
        warning = (of_type(objs, "archive") or [{}])[0].get("warning", "")
        if "Missing slice: %s" % missing not in warning:
            problems.append("warning %r" % warning)
        listed = {o["path"]: o.get("size") for o in of_type(objs, "item")}
        out_dir = os.path.join(work, "out")
        code, objs, x_problems = client.run("x", path, out_dir)
        problems += x_problems + client_exit(code, objs, expected=4)
        item_results = of_type(objs, "item_result")
        bad = [o for o in item_results if o.get("result") != "ok"]
        problems += ["%s: %s" % (o.get("path"), o.get("result")) for o in bad if o.get("result") != "unavailable"]
        ok = sum(o.get("result") == "ok" and o.get("path") in exp.sources for o in item_results)
        if not bad or not ok:
            problems.append("%d files ok, %d unavailable" % (ok, len(bad)))
        ends = of_type(objs, "extract_result")
        if not ends or ends[0].get("hresult") != "0x00000000" or ends[0].get("completed") != ends[0].get("total"):
            problems.append("extract_result %s" % (ends[:1],))
        problems += compare_folder(out_dir, listed, exp.sources, payload, unchecked=[o.get("path") for o in bad])
        results.check(name, problems, "without %s, %d files ok, %d unavailable" % (missing, ok, len(bad)))


# ---------------------------------------------------------------------------
# Encryption and unsupported versions


def encryption_name(compiler):
    return "XChaCha20" if compiler_version(compiler) >= (6, 4, 0) else "ARC4"


def make_unsupported(args, results, installers):
    """A copy of the installer of the newest compiler with its setup data
    version changed to an unknown newer one. Returns its path or None."""
    if not installers:
        return None
    compiler, variant = max(installers, key=lambda k: (compiler_version(k[0]), k))
    with open(installers[(compiler, variant)], "rb") as f:
        data = f.read()
    # The data version, e.g. "(7.0.0.3)", becomes "(9.9.9.9)" (same length).
    patched, count = re.subn(rb"(Inno Setup Setup Data \()([\d.]+)(\))",
                             lambda m: m.group(1) + re.sub(rb"\d", b"9", m.group(2)) + m.group(3), data)
    name = "unsupported version from %s/%s" % (compiler, variant)
    if not count:
        results.report("FAIL", name, "no setup data version found")
        return None
    path = os.path.join(args.work_dir, "unsupported-version.exe")
    with open(path, "wb") as f:
        f.write(patched)
    return path


def test_client_encryption(results, client, installers, unsupported, payload):
    for (compiler, variant), exe in sorted(installers.items()):
        tag = "%s/%s" % (compiler, variant)
        exp = Expected(compiler, variant)
        full = variant == "encrypted-full"

        code, objs, problems = client.run("l", exe, "-p" + PASSWORD)
        problems += client_exit(code, objs)
        archive = (of_type(objs, "archive") or [{}])[0]
        items = of_type(objs, "item")
        problems += compare_listing([(o.get("path"), o.get("size")) for o in items], exp, payload)
        for key in ("error", "error_flags", "warning"):
            if key in archive:
                problems.append("archive %s: %s" % (key, archive[key]))
        if encryption_name(compiler) not in str(archive.get("method", "")).split():
            problems.append("archive method %r without %s" % (archive.get("method"), encryption_name(compiler)))
        files = [o for o in items if not o["path"].startswith("embedded\\")]
        if any(not o.get("encrypted") for o in files if o.get("size")):
            problems.append("files with data that are not encrypted")
        results.check("client l %s" % tag, problems, "right password")
        indices = [o.get("index") for o in items]
        encrypted = sorted(o.get("index") for o in files if o.get("size"))

        code, objs, problems = client.run("t", exe, "-p" + PASSWORD)
        problems += client_exit(code, objs)
        problems += extract_problems(objs, "test", indices)
        results.check("client t %s" % tag, problems, "right password")

        if full:
            # Without the right password, the installer opens without items
            # and reports the encrypted headers (kpv_ErrorFlags_EncryptedHeadersError).
            code, objs, problems = client.run("l", exe, "-p" + WRONG_PASSWORD)
            archive = (of_type(objs, "archive") or [{}])[0]
            if of_type(objs, "item"):
                problems.append("%d items listed" % len(of_type(objs, "item")))
            if not int(archive.get("error_flags", 0)) & (1 << 2):
                problems.append("error_flags %s without EncryptedHeadersError" % archive.get("error_flags"))
            results.check("client l %s" % tag, problems, "wrong password")
            code, objs, problems = client.run("l", exe)
            if code == 0 or of_type(objs, "item"):
                problems.append("exit code %s, %d items" % (code, len(of_type(objs, "item"))))
            results.check("client l %s" % tag, problems, "no password (callback cancels)")
            continue

        # The files with data report kWrongPassword, the others are extracted.
        code, objs, problems = client.run("t", exe, "-p" + WRONG_PASSWORD)
        wrong = sorted(o.get("index") for o in of_type(objs, "item_result") if o.get("result") == "wrong_password")
        others = [o for o in of_type(objs, "item_result") if o.get("index") not in encrypted]
        if wrong != encrypted:
            problems.append("wrong_password for items %s, expected %s" % (wrong[:20], encrypted[:20]))
        problems += ["%s: %s" % (o.get("path"), o.get("result")) for o in others if o.get("result") != "ok"]
        if code == 0:
            problems.append("exit code 0")
        results.check("client t %s" % tag, problems, "wrong password, %d files" % len(encrypted))
        # Without a password the callback cancels the extraction.
        code, objs, problems = client.run("t", exe)
        ends = of_type(objs, "extract_result")
        if not ends or ends[0].get("hresult") != "0x80004004":
            problems.append("extract_result %s, expected E_ABORT" % (ends[0] if ends else None))
        results.check("client t %s" % tag, problems, "no password (callback cancels)")

    if unsupported:
        code, objs, problems = client.run("l", unsupported)
        archive = (of_type(objs, "archive") or [{}])[0]
        if of_type(objs, "item"):
            problems.append("%d items listed" % len(of_type(objs, "item")))
        if "Unsupported Inno Setup version: 9.9.9" not in str(archive.get("error")):
            problems.append("error %r" % archive.get("error"))
        if not int(archive.get("error_flags", 0)) & (1 << 8):
            problems.append("error_flags %s without UnsupportedFeature" % archive.get("error_flags"))
        results.check("client l unsupported version", problems, str(archive.get("error")))


def test_sevenzip_encryption(results, sz, installers, unsupported):
    for (compiler, variant), exe in sorted(installers.items()):
        tag = "%s/%s" % (compiler, variant)
        code, out, err = sz.run("t", "-p" + PASSWORD, sz.path(exe))
        problems = exit_problem(code, err)
        if not problems and "Everything is Ok" not in out:
            problems.append("no 'Everything is Ok'")
        results.check("7z t %s" % tag, problems, "right password")
        code, out, err = sz.run("t", "-p" + WRONG_PASSWORD, sz.path(exe))
        problems = exit_problem(code, err, expected=2)
        if "Wrong password" not in out + err:
            problems.append("no 'Wrong password' in the output")
        results.check("7z t %s" % tag, problems, "wrong password")
    if unsupported:
        code, out, err = sz.run("l", sz.path(unsupported))
        problems = exit_problem(code, err, expected=2)
        if "Unsupported Inno Setup version" not in out + err:
            problems.append("no 'Unsupported Inno Setup version' in the output")
        results.check("7z l unsupported version", problems)


# ---------------------------------------------------------------------------
# Old Inno Setup installers (old-versions.txt), tested as ready-made installers

# Leading folders that separate the items with the same destination.
COLLISION_FOLDER = re.compile(r"^\{(x86|x64|arm64|#[^}]*|@[^}]*|\$\d+)\}$")
# innoextract --collisions rename-all suffixes: #component, @language, @32bit, @64bit, $number.
RENAME_SUFFIX = re.compile(r"(?:[#@][^\\#@$]*|\$\d+)+$")
# Version of the manifests that build_old writes.
MANIFEST_FORMAT = 1
CONVERSION_WARNING = "Unknown data while converting"


def old_installers():
    """[(folder, file name, SHA-256)] from old-versions.txt, oldest first."""
    return [tuple(line.split()) for line in read_text(OLD_LIST).split("\n")
            if line.strip() and not line.startswith("#")]


def old_version(name):
    """File name of an old installer -> (major, minor, patch)."""
    return compiler_version(re.search(r"\d+\.\d+\.\d+", name).group(0))


def old_issues(name):
    """The KNOWN_ISSUES of an old installer."""
    issues = set()
    if old_version(name) < (5, 0, 0):
        issues.add("uninstaller")
    # From 4.2.1 on, the ANSI installers of Inno Setup include languages with other code pages.
    if "unicode" not in name and old_version(name) >= (4, 2, 1):
        issues.add("codepage")
    return issues


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def files_with_hashes(folder):
    """{path relative to folder, with '\\': (size, SHA-256)}."""
    result = {}
    for root, _, names in os.walk(folder):
        for n in names:
            path = os.path.join(root, n)
            result[os.path.relpath(path, folder).replace(os.sep, "\\")] = (os.path.getsize(path), sha256_file(path))
    return result


def build_old(args, results):
    """Copies the old installers that --old-installers selects into
    <corpus>/old and writes what innoextract extracts from each to
    <name>.json (path, size and SHA-256 of each file). Returns [(name, path)]."""
    out = os.path.join(args.corpus_dir, "old")
    innoextract = None if args.no_compile else shutil.which("innoextract")
    version = run([innoextract, "--version"])[1].split("\n")[0].strip() if innoextract else None
    result = []
    for _, name, sha in old_installers():
        if args.old_installers != "all" and str(old_version(name)[0]) not in args.old_installers.split(","):
            continue
        exe = os.path.join(out, name)
        check = "%s old/%s" % ("installer" if args.no_compile else "prepare", name)
        try:
            with open(exe + ".json") as f:
                manifest = json.load(f)
        except (OSError, ValueError):
            manifest = {}
        if (os.path.isfile(exe) and manifest.get("format") == MANIFEST_FORMAT and manifest.get("sha256") == sha
                and (args.no_compile or manifest.get("innoextract") == version) and sha256_file(exe) == sha):
            results.report("PASS", check, "prebuilt" if args.no_compile else "cached")
            result.append((name, exe))
            continue
        if args.no_compile:
            results.report("FAIL", check, "installer or manifest missing or outdated (--no-compile)")
            continue
        source = os.path.join(args.old_dir or "", name)
        if not args.old_dir or not os.path.isfile(source) or sha256_file(source) != sha:
            results.report("FAIL", check, "%s is missing or has another SHA-256 (setup-inno-tools.sh old)" % source)
            continue
        if not innoextract:
            results.report("FAIL", check, "innoextract not found")
            continue
        os.makedirs(out, exist_ok=True)
        shutil.copyfile(source, exe)
        work = os.path.join(args.work_dir, "innoextract", name)
        os.makedirs(work)
        # --dump keeps the paths of Inno Setup ({app}\...). Never -T none: in
        # innoextract 1.9 it truncates the extracted files.
        code, _, err = run([innoextract, "-e", "-s", "-p0", "-c0", "--dump", "--collisions", "rename-all",
                            "-d", work, exe])
        problems = exit_problem(code, err)
        files = files_with_hashes(work)
        if not problems and not files:
            problems.append("innoextract extracted no files")
        if not problems:
            with open(exe + ".json", "w") as f:
                json.dump({"format": MANIFEST_FORMAT, "sha256": sha, "innoextract": version,
                           "files": sorted([p, s, h] for p, (s, h) in files.items())}, f, indent=0)
            result.append((name, exe))
        results.check(check, problems, "%d files" % len(files))
        shutil.rmtree(work, ignore_errors=True)
    return result


def manifest_problems(out_dir, exe, skip=()):
    """Compares an extracted folder with the manifest of an old installer (see
    build_old): the same file contents, and the same paths without the leading
    folders for the same destination and innoextract's renaming. Items under
    embedded\\ and the paths in skip are not compared."""
    with open(exe + ".json") as f:
        manifest = json.load(f)
    got = {p: v for p, v in files_with_hashes(out_dir).items() if p.split("\\")[0] != "embedded" and p not in skip}
    want = {p: (s, h) for p, s, h in manifest["files"]}
    problems = []
    missing = collections.Counter(want.values()) - collections.Counter(got.values())
    extra = collections.Counter(got.values()) - collections.Counter(want.values())
    if missing:
        problems.append("%d files missing or different, e.g. %s" % (
            sum(missing.values()), sorted(p for p, v in want.items() if v in missing)[:3]))
    if extra:
        problems.append("%d files that innoextract does not extract, e.g. %s" % (
            sum(extra.values()), sorted(p for p, v in got.items() if v in extra)[:3]))
    got_paths = collections.Counter("\\".join(c for c in p.lower().split("\\") if not COLLISION_FOLDER.match(c))
                                    for p in got)
    want_paths = collections.Counter("\\".join(fs_components(RENAME_SUFFIX.sub("", p))).lower() for p in want)
    if got_paths != want_paths:
        problems.append("paths differ from innoextract: %s, innoextract %s" % (
            sorted((got_paths - want_paths).elements())[:3], sorted((want_paths - got_paths).elements())[:3]))
    return problems


def test_client_old(args, results, client, old):
    for name, exe in old:
        tag = "old/" + name
        code, objs, problems = client.run("l", exe)
        problems += client_exit(code, objs)
        archive = (of_type(objs, "archive") or [{}])[0]
        items = of_type(objs, "item")
        if archive.get("phy_size") != os.path.getsize(exe):
            problems.append("phy_size %s, file size %d" % (archive.get("phy_size"), os.path.getsize(exe)))
        if "error" in archive:
            problems.append("archive error: %s" % archive["error"])
        symptoms = {}
        warnings = [w for w in archive.get("warning", "").split("\n") if w]
        if any(w.startswith(CONVERSION_WARNING) for w in warnings):
            symptoms["codepage"] = "archive warning %r" % "; ".join(w for w in warnings if w.startswith(CONVERSION_WARNING))
        problems += ["archive warning %r" % w for w in warnings if not w.startswith(CONVERSION_WARNING)]
        empty = [o for o in items if not o.get("path")]
        if empty:
            symptoms["uninstaller"] = "item %s (%s bytes) has an empty path" % (empty[0]["index"], empty[0].get("size"))
        results.check_known("client l " + tag, problems, symptoms, old_issues(name), "%d items" % len(items))
        if not items:
            continue
        indices = [o["index"] for o in items]
        sizes = sum(o.get("size") or 0 for o in items)

        code, objs, problems = client.run("t", exe)
        problems += client_exit(code, objs) + extract_problems(objs, "test", indices, sizes)
        results.check("client t " + tag, problems)

        # The client does not extract items without a path (known issue uninstaller).
        keep = [o["index"] for o in items if o.get("path")]
        option = ["-i" + ",".join(str(i) for i in keep)] if len(keep) < len(indices) else []
        out_dir = os.path.join(args.work_dir, "client-old", name)
        code, objs, problems = client.run("x", exe, out_dir, *option)
        problems += client_exit(code, objs) + extract_problems(objs, "extract", keep, None if option else sizes)
        problems += manifest_problems(out_dir, exe)
        results.check("client x " + tag, problems, "compared with innoextract")
        shutil.rmtree(out_dir, ignore_errors=True)


def test_sevenzip_old(args, results, sz, old):
    for name, exe in old:
        tag = "old/" + name
        code, out, err = sz.run("l", "-slt", sz.path(exe))
        problems = exit_problem(code, err)
        archive, items = parse_slt(out)
        if not problems:
            if archive.get("Type") != "Inno":
                problems.append("Type = %s" % archive.get("Type"))
            if archive.get("Physical Size") != str(os.path.getsize(exe)):
                problems.append("Physical Size = %s, file size %d" % (archive.get("Physical Size"),
                                                                      os.path.getsize(exe)))
        symptoms = {}
        # One warning is on the WARNING line, several follow it in braces.
        converting = [line[line.index(CONVERSION_WARNING):].strip() for line in out.splitlines()
                      if CONVERSION_WARNING in line]
        if converting:
            symptoms["codepage"] = "warning %r" % "; ".join(converting)
        # 7-Zip names an item without a path after the archive.
        stem = os.path.splitext(name)[0]
        if any(i.get("Path") == stem for i in items):
            symptoms["uninstaller"] = "an item without a path, listed as %s" % stem
        results.check_known("7z l " + tag, problems, symptoms, old_issues(name), "%d items" % len(items))

        code, out, err = sz.run("t", sz.path(exe))
        problems = exit_problem(code, err)
        if not problems and "Everything is Ok" not in out:
            problems.append("no 'Everything is Ok'")
        results.check("7z t " + tag, problems)

        out_dir = os.path.join(args.work_dir, "7z-old", name)
        os.makedirs(out_dir, exist_ok=True)
        code, out, err = sz.run("x", "-y", "-o" + sz.path(out_dir), sz.path(exe))
        problems = exit_problem(code, err)
        if not problems:
            problems = manifest_problems(out_dir, exe, skip={stem} if "uninstaller" in symptoms else ())
        results.check("7z x " + tag, problems, "compared with innoextract")
        shutil.rmtree(out_dir, ignore_errors=True)


# ---------------------------------------------------------------------------
# Windows 7-Zip (natively on Windows, under Wine elsewhere)


class SevenZip:
    def __init__(self, folder, env):
        self.folder = folder
        self.exe = os.path.join(folder, "7z.exe")
        if IS_WINDOWS:
            self.env = env
        else:
            # Wine takes TMP and TEMP from its own registry, keep the environment.
            self.env = dict(os.environ)
            self.env.setdefault("WINEDEBUG", "-all")
            self.env.pop("INNO_PLUGIN_LOG", None)

    @staticmethod
    def path(p):
        p = os.path.abspath(p)
        return p if IS_WINDOWS else "Z:" + p.replace("/", "\\")

    def run(self, *args):
        cmd = ([] if IS_WINDOWS else ["wine"]) + [self.exe, "-bd", "-sccUTF-8"] + list(args)
        return run(cmd, env=self.env)


def parse_slt(text):
    """Parses '7z l -slt' output: returns (archive properties, [item properties])."""
    archive, items, current = {}, [], None
    state = 0
    for line in text.splitlines():
        line = ANSI_ESCAPE.sub("", line)
        if state == 0:
            if line == "--":
                state = 1
        elif state == 1:
            if line == "----------":
                state = 2
            elif " = " in line:
                k, v = line.split(" = ", 1)
                archive.setdefault(k, v)
        else:
            if not line.strip():
                current = None
            elif " = " in line or line.endswith(" ="):
                k, v = (line.split(" = ", 1) + [""])[:2] if " = " in line else (line[:-2], "")
                if current is None:
                    current = {}
                    items.append(current)
                current[k] = v
    return archive, items


def make_sfx(args, sz, results):
    """Builds a 7z SFX: 7z.sfx from the 7-Zip folder plus a .7z archive."""
    sfx_module = os.path.join(sz.folder, "7z.sfx")
    work = os.path.join(args.work_dir, "sfx")
    os.makedirs(work, exist_ok=True)
    archive = os.path.join(work, "payload.7z")
    source = os.path.join(args.corpus_dir, "payload", "readme.txt")
    sevenzz = shutil.which("7zz")
    if sevenzz:
        code, out, err = run([sevenzz, "a", "-bd", archive, source])
    else:
        code, out, err = sz.run("a", sz.path(archive), sz.path(source))
    problems = exit_problem(code, err)
    if not os.path.isfile(sfx_module):
        problems.append("%s not found" % sfx_module)
    if problems:
        results.report("FAIL", "7z l 7z SFX", "cannot build the SFX: " + "; ".join(problems))
        return None
    sfx = os.path.join(work, "sfx.exe")
    with open(sfx, "wb") as f:
        for part in (sfx_module, archive):
            with open(part, "rb") as p:
                shutil.copyfileobj(p, f)
    return sfx


def prepare_sevenzip(args, results):
    """Returns the 7-Zip folder to use, or None (check skipped or failed)."""
    if not args.sevenzip_dir:
        results.report("SKIP", "7-Zip end-to-end", "no 7-Zip folder (--sevenzip-dir)")
        return None
    if not os.path.isfile(os.path.join(args.sevenzip_dir, "7z.exe")):
        results.report("FAIL", "7-Zip end-to-end", "no 7z.exe in %s" % args.sevenzip_dir)
        return None
    if not IS_WINDOWS and not shutil.which("wine"):
        results.report("FAIL", "7-Zip end-to-end", "wine not found")
        return None
    if not args.sevenzip_plugin:
        return args.sevenzip_dir
    if not os.path.isfile(args.sevenzip_plugin):
        results.report("FAIL", "7-Zip end-to-end", "%s not found" % args.sevenzip_plugin)
        return None
    # A private copy, so the 7-Zip folder itself is not changed.
    folder = os.path.join(args.work_dir, "7-Zip")
    os.makedirs(os.path.join(folder, "Formats"))
    for name in ("7z.exe", "7z.dll", "7z.sfx"):
        src = os.path.join(args.sevenzip_dir, name)
        if os.path.isfile(src):
            shutil.copy2(src, folder)
    shutil.copy2(args.sevenzip_plugin, os.path.join(folder, "Formats"))
    return folder


def file_mtimes(out_dir, paths):
    """Returns {path: modification time of the extracted file as FILETIME}."""
    result = {}
    for path in paths:
        try:
            ns = os.stat(os.path.join(out_dir, *fs_components(path))).st_mtime_ns
        except OSError:
            continue
        result[path] = filetime(0) + ns // 100
    return result


def test_sevenzip(args, results, installers, expected, damaged, payload, env, encrypted=None, unsupported=None,
                  old=()):
    folder = prepare_sevenzip(args, results)
    if not folder:
        return
    sz = SevenZip(folder, env)
    test_sevenzip_encryption(results, sz, encrypted or {}, unsupported)
    test_sevenzip_old(args, results, sz, old)

    code, out, err = sz.run("i")
    problems = exit_problem(code, err)
    shown = []
    if not problems:
        formats_dir = sz.path(os.path.join(folder, "Formats")).lower() + "\\"
        libs = {}
        for line in out.splitlines():
            m = re.match(r"\s*(\d+)\s*:\s*\S+\s*:\s*(.+?)\s*$", line)
            if m:
                libs[m.group(1)] = m.group(2)
        # Format lines: library index, flags, name, extensions, signature.
        inno = [line for line in out.splitlines() if "Inno" in line.split()[1:4]]
        if not inno:
            problems.append("no Inno format")
        for line in inno:
            lib = libs.get(line.split()[0], "")
            shown += ["%s : %s" % (line.split()[0], lib), line.strip()]
            if not lib.lower().startswith(formats_dir):
                problems.append("Inno comes from %r, not from %s" % (lib, formats_dir))
    results.check("7z i", problems)
    for line in shown:
        print("      " + line)

    for (compiler, variant), exe in sorted(installers.items()):
        tag = "%s/%s" % (compiler, variant)
        exp = expected[(compiler, variant)]
        code, out, err = sz.run("l", "-slt", sz.path(exe))
        problems = exit_problem(code, err)
        archive, items = parse_slt(out)
        if not problems:
            if archive.get("Type") != "Inno":
                problems.append("Type = %s" % archive.get("Type"))
            if archive.get("Physical Size") != str(os.path.getsize(exe)):
                problems.append("Physical Size = %s, file size %d" % (archive.get("Physical Size"),
                                                                      os.path.getsize(exe)))
            problems += compare_listing([(i.get("Path"), int(i["Size"]) if i.get("Size") else 0)
                                         for i in items], exp, payload)
            problems += method_problems(exp, archive.get("Method"),
                                        [(i.get("Path"), i.get("Method")) for i in items])
        symptoms = {}
        if "Missing slice" in out:
            symptoms["slices-1.3"] = "warning: " + "; ".join(l.strip() for l in out.splitlines()
                                                             if "Missing slice" in l)
        issues = installer_issues(compiler, variant)
        # With a known issue the other checks of the installer would fail too.
        if results.check_known("7z l %s" % tag, problems, symptoms, issues) and issues:
            continue
        listed = {i.get("Path"): int(i["Size"]) if i.get("Size") else 0 for i in items}

        out_dir = os.path.join(args.work_dir, "7z", compiler, variant)
        os.makedirs(out_dir, exist_ok=True)
        before = expected_mtimes(variant, exp) if variant in TIME_VARIANTS else None
        code, out, err = sz.run("x", "-y", "-o" + sz.path(out_dir), sz.path(exe))
        problems = exit_problem(code, err)
        if not problems:
            problems = compare_folder(out_dir, listed, exp.sources, payload)
        extracted = not problems
        detail = ""
        if before and not problems:
            # 7-Zip sets the file times from kpidMTime.
            problems = mtime_problems(before, expected_mtimes(variant, exp), file_mtimes(out_dir, before))
            detail = "file times: stored in UTC" if TIME_VARIANTS[variant] else \
                "file times: stored in local time, converted in the local time zone"
        results.check("7z x %s" % tag, problems, detail)

        code, out, err = sz.run("t", sz.path(exe))
        problems = exit_problem(code, err)
        if not problems and "Everything is Ok" not in out:
            problems.append("no 'Everything is Ok'")
        results.check("7z t %s" % tag, problems)

        if extracted:
            changed, deleted, content = modify_output(out_dir, exp)
            code, out, err = sz.run("x", "-aos", "-o" + sz.path(out_dir), sz.path(exe))
            problems = exit_problem(code, err)
            problems += compare_folder(out_dir, listed, exp.sources, payload, unchecked=[changed])
            with open(os.path.join(out_dir, *fs_components(changed)), "rb") as f:
                if f.read() != content:
                    problems.append("%s was overwritten" % changed)
            results.check("7z x -aos %s" % tag, problems, "skip existing files, deleted %s" % deleted)

    # Detection without the .exe extension (IsArc_Inno).
    for compiler in args.compilers:
        exe = installers.get((compiler, "lzma-solid"))
        if not exe:
            continue
        renamed = os.path.join(args.work_dir, "renamed", compiler, "lzma-solid.bin")
        os.makedirs(os.path.dirname(renamed), exist_ok=True)
        shutil.copyfile(exe, renamed)
        code, out, err = sz.run("l", "-slt", sz.path(renamed))
        problems = exit_problem(code, err)
        if not problems and parse_slt(out)[0].get("Type") != "Inno":
            problems.append("Type = %s" % parse_slt(out)[0].get("Type"))
        results.check("7z l %s/lzma-solid as .bin" % compiler, problems)

    for (compiler, kind), (path, detail) in sorted(damaged.items()):
        variant = "none" if kind == "stored" else "lzma-solid"
        exp = expected[(compiler, variant)]
        name = "7z damaged %s/%s-%s" % (compiler, variant, kind)
        if kind in DECLINED_KINDS:
            # Not opened as Inno (7-Zip may fall back to another handler).
            code, out, err = sz.run("l", "-slt", sz.path(path))
            problems = [] if code == 2 else exit_problem(code, err)
            if code == 0 and parse_slt(out)[0].get("Type") == "Inno":
                problems.append("opened as Inno")
            results.check(name, problems, detail + ", exit code %s" % code)
            continue
        code, out, err = sz.run("t", sz.path(path))
        problems = exit_problem(code, err, expected=2)
        if kind == "stored":
            code, out, err = sz.run("l", "-slt", sz.path(installers[(compiler, variant)]))
            listed = {i.get("Path"): int(i["Size"]) if i.get("Size") else 0 for i in parse_slt(out)[1]}
            out_dir = os.path.join(args.work_dir, "7z-damaged", compiler, kind)
            os.makedirs(out_dir, exist_ok=True)
            code, out, err = sz.run("x", "-y", "-o" + sz.path(out_dir), sz.path(path))
            problems += exit_problem(code, err, expected=2)
            problems += compare_folder(out_dir, listed, exp.sources, payload, unchecked=[
                p for p, s in exp.sources.items() if s == "data/random.bin"])
        results.check(name, problems, detail)

    # Executables that are not Inno installers still open with 7-Zip's own handlers.
    for name, path, want in (("7z l 7z.exe", sz.exe, "PE"),
                             ("7z l 7z SFX", make_sfx(args, sz, results), "7z")):
        if not path:
            continue
        code, out, err = sz.run("l", "-slt", sz.path(path))
        problems = exit_problem(code, err)
        if not problems:
            archive, _ = parse_slt(out)
            if archive.get("Type") != want:
                problems.append("Type = %s, expected %s" % (archive.get("Type"), want))
        results.check(name, problems)


# ---------------------------------------------------------------------------


def main():
    exe = ".exe" if IS_WINDOWS else ""
    if IS_WINDOWS:
        default_build = os.path.join(BUILD_DIR, "x64", "Release")
        default_plugin = os.path.join(default_build, "Inno.64.dll")
        default_7z, default_7z_plugin = None, None
    else:
        default_build = os.path.join(BUILD_DIR, "linux")
        default_plugin = os.path.join(default_build, "Inno.so")
        default_7z = "/opt/inno-tools/7zip-win64"
        default_7z_plugin = os.path.join(BUILD_DIR, "mingw-x86_64", "Inno.64.dll")
        if not (os.path.isdir(default_7z) and os.path.isfile(default_7z_plugin)):
            default_7z, default_7z_plugin = None, None

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client", default=os.path.join(default_build, "inno_client" + exe),
                    help="test client (default: %(default)s)")
    ap.add_argument("--plugin", default=default_plugin, help="plugin for the client (default: %(default)s)")
    ap.add_argument("--sevenzip-dir", default=default_7z,
                    help="Windows 7-Zip folder for the end-to-end tests; without --sevenzip-plugin the "
                         "plugin must already be in its Formats folder (default: %(default)s)")
    ap.add_argument("--sevenzip-plugin", default=default_7z_plugin,
                    help="plugin DLL to install into a private copy of --sevenzip-dir (default: %(default)s)")
    ap.add_argument("--no-client", action="store_true", help="skip the test client checks")
    ap.add_argument("--no-sevenzip", action="store_true", help="skip the 7-Zip end-to-end tests")
    ap.add_argument("--corpus-dir", default=os.path.join(BUILD_DIR, "corpus"),
                    help="payload and compiled installers, also the compile cache (default: %(default)s)")
    ap.add_argument("--no-compile", action="store_true",
                    help="use the installers in --corpus-dir as they are (prebuilt corpus, e.g. on Windows)")
    ap.add_argument("--compile-only", action="store_true",
                    help="only compile (or reuse) the installers in --corpus-dir, then stop")
    ap.add_argument("--compilers", default=",".join(COMPILERS + OLD_COMPILERS),
                    help="comma-separated Inno Setup compiler labels (iscc-<label>) (default: %(default)s)")
    ap.add_argument("--old-installers", default="all",
                    help="old Inno Setup installers (old-versions.txt) to test: all, none, or comma-separated "
                         "major versions such as 1,2,3,4 (default: %(default)s)")
    ap.add_argument("--old-dir", default=None if IS_WINDOWS else "/opt/inno-tools/old",
                    help="the downloaded old installers (setup-inno-tools.sh old), for building the "
                         "corpus (default: %(default)s)")
    ap.add_argument("--work-dir", default=os.path.join(BUILD_DIR, "test-work"),
                    help="scratch folder, emptied on start (default: %(default)s)")
    args = ap.parse_args()
    args.compilers = [c for c in args.compilers.split(",") if c]
    for name in ("client", "plugin", "sevenzip_dir", "sevenzip_plugin", "corpus_dir", "old_dir", "work_dir"):
        if getattr(args, name):
            setattr(args, name, os.path.abspath(getattr(args, name)))
    if args.no_sevenzip:
        args.sevenzip_dir = None

    print("client:   %s\nplugin:   %s\n7-Zip:    %s%s\ncorpus:   %s" % (
        args.client, args.plugin, args.sevenzip_dir or "-",
        " + " + args.sevenzip_plugin if args.sevenzip_dir and args.sevenzip_plugin else "",
        args.corpus_dir), flush=True)

    shutil.rmtree(args.work_dir, ignore_errors=True)
    tmp = os.path.join(args.work_dir, "tmp")
    os.makedirs(tmp)
    # The plugin must not write files: give it an empty temporary folder and
    # check that it stays empty. Its debug log is off unless INNO_PLUGIN_LOG is set.
    env = dict(os.environ)
    env.pop("INNO_PLUGIN_LOG", None)
    for var in ("TMPDIR", "TMP", "TEMP"):
        env[var] = tmp
    if not IS_WINDOWS:
        # The client converts local times with the offset of this time zone.
        env["TZ"] = TIME_ZONE

    results = Results()
    payload = dict(make_payload.files())
    expected = {(c, v): Expected(c, v) for c in args.compilers for v in variants(c)}
    installers = build_corpus(args, results)
    encrypted = build_corpus(args, results, encrypted=True)
    old = build_old(args, results)
    if args.compile_only:
        print("\nSummary: " + results.summary())
        return 1 if results.failed() else 0
    damaged = make_damaged(args, results, installers, payload)
    unsupported = make_unsupported(args, results, installers)
    if args.no_client:
        results.report("SKIP", "client", "--no-client")
    else:
        test_client(args, results, installers, expected, damaged, payload, env)
        if os.path.isfile(args.client) and os.path.isfile(args.plugin):
            test_client_encryption(results, Client(args, env), encrypted, unsupported, payload)
            test_client_old(args, results, Client(args, env), old)
    test_sevenzip(args, results, installers, expected, damaged, payload, env, encrypted, unsupported, old)
    left = sorted(os.listdir(tmp))
    results.check("no files in the temporary folder", ["left: " + ", ".join(left[:5])] if left else [])

    print("\nSummary: " + results.summary())
    return 1 if results.failed() else 0


if __name__ == "__main__":
    sys.exit(main())
