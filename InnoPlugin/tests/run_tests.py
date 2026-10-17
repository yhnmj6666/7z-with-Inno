#!/usr/bin/env python3
"""Test runner for the Inno plugin (see README.md in this folder).

Builds the corpus (payload plus installers compiled from corpus/*.iss), then
checks the plugin through the test client and, when available, through the
Windows 7-Zip: list, extract and test every installer, and compare the result
with the payload. Prints one line per check and a summary; the exit code is 0
only if nothing failed.
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

COMPILERS = ["5.6.1-unicode", "6.0.5", "6.1.2", "6.2.2"]
VARIANTS = ["lzma-solid", "lzma2-nonsolid", "none"]
TIMEOUT = 600

# Characters that Windows does not allow in file names. The test client and
# 7-Zip on Windows replace them with '_' in each path component.
INCORRECT_CHARS = re.compile(r'[\x00-\x1f:*?<>|"]')


def fs_components(archive_path):
    """Archive path ('\\'-separated) -> output path components."""
    return [INCORRECT_CHARS.sub("_", c) for c in archive_path.split("\\") if c]


def is_generated_item(path):
    """Items the handler adds besides the [Files] entries (unchanged until Phase 4)."""
    return path == "unins___.exe" or path.startswith("embedded\\")


def read_text(path):
    with open(path, "rb") as f:
        # Line endings depend on the checkout (core.autocrlf); hash and parse without them.
        return f.read().replace(b"\r\n", b"\n").decode("ascii")


class Results:
    def __init__(self):
        self.counts = dict.fromkeys(["PASS", "FAIL", "XFAIL", "XPASS", "SKIP"], 0)

    def report(self, status, name, detail=""):
        self.counts[status] += 1
        print("%-5s %s%s" % (status, name, ": " + detail if detail else ""), flush=True)

    def check(self, name, problems):
        if problems:
            self.report("FAIL", name, "; ".join(problems[:5]) + (" ..." if len(problems) > 5 else ""))
        else:
            self.report("PASS", name)

    def expect_failure(self, name, failed, problems):
        """For known problems: failed=True is XFAIL, a success is XPASS (counted as a failure)."""
        if problems:
            self.report("FAIL", name, "; ".join(problems))
        elif failed:
            self.report("XFAIL", name, "known problem, fixed in Phase 4")
        else:
            self.report("XPASS", name, "passed unexpectedly, update the expectation")

    def failed(self):
        return self.counts["FAIL"] + self.counts["XPASS"] > 0

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


def expected_entries(variant):
    """Returns [(archive path, payload path with '/')] from the [Files] sections."""
    entries = []
    section = None
    for path in script_files(variant):
        for line in read_text(path).split("\n"):
            s = line.strip()
            if not s or s.startswith(";") or s.startswith("#"):
                continue
            if s.startswith("["):
                section = s.lower()
                continue
            if section != "[files]":
                continue
            params = {k.lower(): v for k, v in re.findall(r'(\w+)\s*:\s*"([^"]*)"', s)}
            source = params["source"].split("\\")
            if source[0] != "payload" or any(c in params["source"] for c in "*?"):
                raise ValueError("unsupported [Files] entry in %s: %s" % (path, s))
            name = params.get("destname", source[-1])
            entries.append((params["destdir"].rstrip("\\") + "\\" + name, "/".join(source[1:])))
    return entries


def corpus_key(compiler, variant):
    h = hashlib.sha256()
    h.update(("%s\n%s\n" % (compiler, variant)).encode("ascii"))
    for path in script_files(variant) + [os.path.join(TESTS_DIR, "make_payload.py")]:
        h.update(read_text(path).encode("ascii"))
    return h.hexdigest()


def find_iscc(compiler):
    return shutil.which("iscc-" + compiler)


def build_corpus(args, results):
    """Writes the payload, compiles (or reuses) the installers. Returns {(compiler, variant): exe}."""
    corpus = args.corpus_dir
    installers = {}
    if args.no_compile:
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
        for variant in VARIANTS:
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
                results.report("FAIL", name, "installer missing or built from other scripts (--no-compile)")
                continue
            iscc = find_iscc(compiler)
            if not iscc:
                results.report("FAIL", name, "iscc-%s not found" % compiler)
                continue
            os.makedirs(out_dir, exist_ok=True)
            for f in (exe, key_file):
                if os.path.exists(f):
                    os.remove(f)
            code, out, err = run([iscc, "/Q", "/O" + out_dir, "/F" + variant,
                                  os.path.join(corpus, variant + ".iss")], cwd=corpus)
            problems = exit_problem(code, (err + out)[-500:])
            if not problems and not os.path.isfile(exe):
                problems.append("no output file")
            results.check(name, problems)
            if not problems:
                with open(key_file, "w") as f:
                    f.write(key + "\n")
                installers[(compiler, variant)] = exe
    return installers


# ---------------------------------------------------------------------------
# Comparing with the payload


def compare_listing(items, entries, payload):
    """items: [(path, size)] as listed. Returns a list of problems."""
    problems = []
    listed = {}
    for path, size in items:
        if path in listed:
            problems.append("duplicate path %s" % path)
        listed[path] = size
    for path, source in entries:
        if path not in listed:
            problems.append("missing %s" % path)
        elif listed[path] != len(payload[source]):
            problems.append("%s: size %s, expected %d" % (path, listed[path], len(payload[source])))
    expected = {path for path, _ in entries}
    for path in listed:
        if path not in expected and not is_generated_item(path):
            problems.append("unexpected item %s" % path)
    return problems


def compare_folder(out_dir, entries, payload):
    """Compares extracted files with the payload. Returns a list of problems."""
    problems = []
    expected = {}
    for path, source in entries:
        expected[tuple(fs_components(path))] = source
    found = set()
    for root, _, names in os.walk(out_dir):
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), out_dir)
            found.add(tuple(rel.split(os.sep)))
    for comps, source in sorted(expected.items()):
        if comps not in found:
            problems.append("not extracted: %s" % "/".join(comps))
            continue
        with open(os.path.join(out_dir, *comps), "rb") as f:
            data = f.read()
        if data != payload[source]:
            problems.append("%s: content differs from payload/%s (size %d, expected %d)"
                            % ("/".join(comps), source, len(data), len(payload[source])))
    for comps in sorted(found):
        if comps not in expected and comps != ("unins___.exe",) and comps[0] != "embedded":
            problems.append("unexpected file %s" % "/".join(comps))
    return problems


# ---------------------------------------------------------------------------
# Test client


def parse_json_lines(text):
    objs = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{"):
            objs.append(json.loads(line))
    return objs


def test_client(args, results, installers, entries, payload, env):
    client, plugin = args.client, args.plugin
    for f in (client, plugin):
        if not os.path.isfile(f):
            results.report("FAIL", "client", "%s not found (build it or pass --client/--plugin)" % f)
            return
    code, out, err = run([client, plugin, "i"], env=env)
    problems = exit_problem(code, err)
    if not problems:
        formats = [o for o in parse_json_lines(out) if o.get("type") == "format"]
        if not any(o.get("name") == "Inno" and o.get("ext") == "exe" for o in formats):
            problems.append("no Inno format with extension exe")
    results.check("client i", problems)

    for (compiler, variant), exe in sorted(installers.items()):
        tag = "%s/%s" % (compiler, variant)
        code, out, err = run([client, plugin, "l", exe], env=env)
        problems = exit_problem(code, err)
        if not problems:
            objs = parse_json_lines(out)
            items = [(o["path"], o.get("size")) for o in objs if o.get("type") == "item"]
            problems = compare_listing(items, entries[variant], payload)
        results.check("client l %s" % tag, problems)

        out_dir = os.path.join(args.work_dir, "client", compiler, variant)
        code, out, err = run([client, plugin, "x", exe, out_dir], env=env)
        problems = exit_problem(code, err)
        if not problems:
            problems = compare_folder(out_dir, entries[variant], payload)
        results.check("client x %s" % tag, problems)

        # Test mode fails: the handler returns E_FAIL when GetStream gives no stream.
        code, out, err = run([client, plugin, "t", exe], env=env)
        results.expect_failure("client t %s" % tag, code == 3, [] if code in (0, 3) else exit_problem(code, err))


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


def test_sevenzip(args, results, installers, entries, payload, env):
    folder = prepare_sevenzip(args, results)
    if not folder:
        return
    sz = SevenZip(folder, env)

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
        code, out, err = sz.run("l", "-slt", sz.path(exe))
        problems = exit_problem(code, err)
        if not problems:
            archive, items = parse_slt(out)
            if archive.get("Type") != "Inno":
                problems.append("Type = %s" % archive.get("Type"))
            listed = [(i.get("Path"), int(i["Size"]) if i.get("Size") else 0) for i in items]
            problems += compare_listing(listed, entries[variant], payload)
        results.check("7z l %s" % tag, problems)

        out_dir = os.path.join(args.work_dir, "7z", compiler, variant)
        os.makedirs(out_dir, exist_ok=True)
        code, out, err = sz.run("x", "-y", "-o" + sz.path(out_dir), sz.path(exe))
        problems = exit_problem(code, err)
        if not problems:
            problems = compare_folder(out_dir, entries[variant], payload)
        results.check("7z x %s" % tag, problems)

        code, out, err = sz.run("t", sz.path(exe))
        results.expect_failure("7z t %s" % tag, code == 2, [] if code in (0, 2) else exit_problem(code, err))

    # Executables that are not Inno installers still open with 7-Zip's own handlers.
    for name, path, expected in (("7z l 7z.exe", sz.exe, "PE"),
                                 ("7z l 7z SFX", make_sfx(args, sz, results), "7z")):
        if not path:
            continue
        code, out, err = sz.run("l", "-slt", sz.path(path))
        problems = exit_problem(code, err)
        if not problems:
            archive, _ = parse_slt(out)
            if archive.get("Type") != expected:
                problems.append("Type = %s, expected %s" % (archive.get("Type"), expected))
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
    os.makedirs(os.path.join(args.work_dir, "tmp"))
    # The handler writes a debug log into the temporary folder on every open
    # (known problem); keep those files in the work folder.
    env = dict(os.environ)
    for var in ("TMPDIR", "TMP", "TEMP"):
        env[var] = os.path.join(args.work_dir, "tmp")

    results = Results()
    payload = dict(make_payload.files())
    entries = {v: expected_entries(v) for v in VARIANTS}
    installers = build_corpus(args, results)
    if args.no_client:
        results.report("SKIP", "client", "--no-client")
    else:
        test_client(args, results, installers, entries, payload, env)
    test_sevenzip(args, results, installers, entries, payload, env)

    print("\nSummary: " + results.summary())
    return 1 if results.failed() else 0


if __name__ == "__main__":
    sys.exit(main())
