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
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
PLUGIN_DIR = os.path.dirname(TESTS_DIR)
SCRIPTS_DIR = os.path.join(TESTS_DIR, "corpus")
BUILD_DIR = os.path.join(PLUGIN_DIR, "build")
IS_WINDOWS = os.name == "nt"

sys.path.insert(0, TESTS_DIR)
import make_payload  # noqa: E402

COMPILERS = ["5.6.1", "5.6.1-unicode", "6.0.5", "6.1.2", "6.2.2", "6.3.3", "6.4.3", "6.5.4",
             "6.6.1", "6.7.3", "7.0.2-x64", "7.1.0-x64", "7.1.0-x86"]
# Compilers whose preprocessor does not define UNICODE (ANSI installers).
ANSI_COMPILERS = {"5.6.1"}
# Variant -> compression method of its files, as kpidMethod names it.
VARIANTS = {
    "lzma-solid": "LZMA",
    "lzma2-nonsolid": "LZMA2",
    "zip-solid": "Deflate",
    "zip-nonsolid": "Deflate",
    "bzip-solid": "BZip2",
    "bzip-nonsolid": "BZip2",
    "none": "Copy",
    "time-utc": "LZMA2",
    "time-local": "LZMA2",
}
# Encrypted variants -> first Inno Setup version that can compile them. They
# are tested separately (test_encryption) because they need the password.
# Before 6.4, Encryption=yes uses ARC4 from ISCrypt.dll, which the compiler
# needs in its folder; without it the variant is skipped.
ENCRYPTED_VARIANTS = {"encrypted": (5, 0, 0), "encrypted-full": (6, 5, 0)}
PASSWORD = "inno-test-password"
WRONG_PASSWORD = "wrong-password"
# Timestamp variants -> True if they store times in UTC (TimeStampsInUTC=yes).
TIME_VARIANTS = {"time-utc": True, "time-local": False}
# On Linux, the client lists the timestamp variants in this time zone.
TIME_ZONE = "America/New_York"
# Payload files that are x86 executables. Inno Setup 5.3.9 and later store them
# with this call instruction filter when they are compressed.
EXECUTABLES = {"bin/calls.exe"}
FILTER = "IF5309"
TIMEOUT = 600

# Characters that Windows does not allow in file names. The test client and
# 7-Zip on Windows replace them with '_' in each path component.
INCORRECT_CHARS = re.compile(r'[\x00-\x1f:*?<>|"]')
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
# Extra items whose number depends on the compiler (wizard images).
OPTIONAL_EXTRA = re.compile(r"embedded\\WizardImage(Small)?\d+\.bmp$")
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
        self.counts = dict.fromkeys(["PASS", "FAIL", "SKIP"], 0)

    def report(self, status, name, detail=""):
        self.counts[status] += 1
        print("%-5s %s%s" % (status, name, ": " + detail if detail else ""), flush=True)

    def check(self, name, problems, detail=""):
        if problems:
            self.report("FAIL", name, (detail + "; " if detail else "") + "; ".join(problems[:5])
                        + (" ..." if len(problems) > 5 else ""))
        else:
            self.report("PASS", name, detail)

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
    """Yields the lines of a corpus script with #include files inserted and
    #ifdef, #ifndef, #else and #endif applied. defined: the preprocessor
    symbols (#define adds to it)."""
    active = []
    for line in read_text(path).split("\n"):
        s = line.strip()
        if not s.startswith("#"):
            if all(active):
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
            defined.add(words[1])
        else:
            raise ValueError("%s: unsupported preprocessor line: %s" % (path, s))
    if active:
        raise ValueError("%s: #ifdef without #endif" % path)


class Expected:
    """What a corpus installer contains, from its scripts (see corpus/common.iss).

    entries: [(item path, payload path with '/')] for the [Files] entries;
    sources: the same as a dict; extras: the embedded items that must be
    listed (the wizard images are optional, their number depends on the
    compiler)."""

    def __init__(self, compiler, variant):
        tag = "%s/%s" % (compiler, variant)
        payload_names = [rel for rel, _ in make_payload.files()]
        defined = set() if compiler in ANSI_COMPILERS else {"UNICODE"}
        self.entries, languages = [], []
        section, expect = None, None
        for line in script_lines(os.path.join(SCRIPTS_DIR, variant + ".iss"), defined):
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
        # Without a [Languages] section, Inno Setup names the language "default".
        self.extras = ["embedded\\install_script.iss"] + [
            "embedded\\%s.isl" % name for name in languages or ["default"]]


def corpus_key(compiler, variant):
    h = hashlib.sha256()
    h.update(("%s\n%s\n" % (compiler, variant)).encode("ascii"))
    for path in script_files(variant) + [os.path.join(TESTS_DIR, "make_payload.py")]:
        h.update(read_text(path).encode("ascii"))
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


def encrypted_variants(compiler):
    return [v for v, first in ENCRYPTED_VARIANTS.items() if compiler_version(compiler) >= first]


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
        for variant in (encrypted_variants(compiler) if encrypted else VARIANTS):
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
            for f in (exe, key_file):
                if os.path.exists(f):
                    os.remove(f)
            # Installers store local times unless TimeStampsInUTC=yes; compile in UTC.
            code, out, err = run([iscc, "/Q", "/O" + out_dir, "/F" + variant,
                                  os.path.join(corpus, variant + ".iss")], cwd=corpus,
                                 env=dict(os.environ, TZ="UTC"))
            problems = exit_problem(code, (err + out)[-500:])
            if not problems and not os.path.isfile(exe):
                problems.append("no output file")
            results.check(name, problems)
            if not problems:
                with open(key_file, "w") as f:
                    f.write(key + "\n")
                installers[(compiler, variant)] = exe
    return installers


def make_damaged(args, results, installers, payload):
    """Writes damaged copies of corpus installers. Returns {(compiler, kind): (path, detail)}.

    Inno Setup writes the setup headers after the file data, so a truncated
    installer cannot be opened at all; flipped bytes in the file data give
    per-item errors:
      stored     none.exe, one bit flipped inside data/random.bin
      solid      lzma-solid.exe, one byte changed in the middle of its chunk
      truncated  lzma-solid.exe cut in the middle of its chunk
      headers    lzma-solid.exe cut inside the setup headers
    """
    damaged = {}
    for compiler in args.compilers:
        for kind, variant in (("stored", "none"), ("solid", "lzma-solid"),
                              ("truncated", "lzma-solid"), ("headers", "lzma-solid")):
            exe = installers.get((compiler, variant))
            if not exe:
                continue
            with open(exe, "rb") as f:
                data = bytearray(f.read())
            problem = None
            if kind == "stored":
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
            detail = "%s at offset %d" % ("cut" if kind in ("truncated", "headers") else "changed byte", offset)
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
    for comps in sorted(found - set(expected) - tolerated):
        problems.append("unexpected file %s" % "\\".join(comps))
    return problems


def method_problems(variant, expected, archive_method, items):
    """Checks kpidMethod: the variant's method, plus the call instruction filter
    for compressed executables. items: [(path, method)]."""
    method = VARIANTS[variant]
    filtered = method if method == "Copy" else "%s %s" % (method, FILTER)
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
        for key in ("error", "warning"):
            if key in archive:
                problems.append("archive %s: %s" % (key, archive[key]))
        results.check("client l %s" % tag, problems)
        if not items:
            continue
        listed = {o["path"]: o.get("size") for o in items}
        indices = {o["path"]: o["index"] for o in items}
        all_indices = sorted(indices.values())
        sizes = sum(listed.values())

        results.check("client method %s" % tag, method_problems(
            variant, exp, archive.get("method"), [(o["path"], o.get("method")) for o in items]))
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


def test_client_damaged(args, results, client, installers, expected, damaged, payload):
    for (compiler, kind), (path, detail) in sorted(damaged.items()):
        variant = "none" if kind == "stored" else "lzma-solid"
        exp = expected[(compiler, variant)]
        name = "client damaged %s/%s-%s" % (compiler, variant, kind)
        if kind in ("truncated", "headers"):
            # Never a crash or hang: the installer is declined.
            code, objs, problems = client.run("l", path)
            problems += client_exit(code, objs, expected=2)
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


def test_sevenzip(args, results, installers, expected, damaged, payload, env, encrypted=None, unsupported=None):
    folder = prepare_sevenzip(args, results)
    if not folder:
        return
    sz = SevenZip(folder, env)
    test_sevenzip_encryption(results, sz, encrypted or {}, unsupported)

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
            problems += method_problems(variant, exp, archive.get("Method"),
                                        [(i.get("Path"), i.get("Method")) for i in items])
        results.check("7z l %s" % tag, problems)
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
        if kind in ("truncated", "headers"):
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
    ap.add_argument("--compilers", default=",".join(COMPILERS),
                    help="comma-separated Inno Setup compiler labels (iscc-<label>) (default: %(default)s)")
    ap.add_argument("--work-dir", default=os.path.join(BUILD_DIR, "test-work"),
                    help="scratch folder, emptied on start (default: %(default)s)")
    args = ap.parse_args()
    args.compilers = [c for c in args.compilers.split(",") if c]
    for name in ("client", "plugin", "sevenzip_dir", "sevenzip_plugin", "corpus_dir", "work_dir"):
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
    expected = {(c, v): Expected(c, v) for c in args.compilers for v in VARIANTS}
    installers = build_corpus(args, results)
    encrypted = build_corpus(args, results, encrypted=True)
    damaged = make_damaged(args, results, installers, payload)
    unsupported = make_unsupported(args, results, installers)
    if args.no_client:
        results.report("SKIP", "client", "--no-client")
    else:
        test_client(args, results, installers, expected, damaged, payload, env)
        if os.path.isfile(args.client) and os.path.isfile(args.plugin):
            test_client_encryption(results, Client(args, env), encrypted, unsupported, payload)
    test_sevenzip(args, results, installers, expected, damaged, payload, env, encrypted, unsupported)
    left = sorted(os.listdir(tmp))
    results.check("no files in the temporary folder", ["left: " + ", ".join(left[:5])] if left else [])

    print("\nSummary: " + results.summary())
    return 1 if results.failed() else 0


if __name__ == "__main__":
    sys.exit(main())
