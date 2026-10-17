#!/usr/bin/env python3
"""Checks the exports and imports of plugin DLLs.

Usage: check_dll.py [--dumpbin PATH|auto] DLL...

Each DLL must export exactly the names in Archive.def and import only Windows
system DLLs (msvcrt.dll, which MinGW links against, counts as one); C and C++
runtime DLLs such as VCRUNTIME*, MSVCP*, api-ms-win-crt-*, libstdc++ or libgcc
are not allowed. Reads the DLLs with "objdump -p", or with
"dumpbin /exports /dependents" when --dumpbin is given ("auto" takes dumpbin
from PATH or from the latest Visual Studio found by vswhere). Prints one line
per check; the exit code is 0 only if all checks pass.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_FILE = os.path.join(PLUGIN_DIR, "third_party", "7zip", "CPP", "7zip", "Archive", "Archive.def")

SYSTEM_DLLS = {
    "advapi32.dll", "gdi32.dll", "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll",
    "oleaut32.dll", "shell32.dll", "shlwapi.dll", "user32.dll", "version.dll",
}


def def_exports(path):
    names = set()
    in_exports = False
    with open(path) as f:
        for line in f:
            line = line.split(";", 1)[0].strip()
            if not line:
                continue
            if line.upper() == "EXPORTS":
                in_exports = True
            elif in_exports:
                names.add(line.split()[0])
    return names


def read_objdump(objdump, dll):
    out = subprocess.run([objdump, "-p", dll], check=True, stdout=subprocess.PIPE,
                         universal_newlines=True).stdout
    imports = re.findall(r"^\s*DLL Name:\s*(\S+)", out, re.M)
    exports = set()
    in_names = False
    for line in out.splitlines():
        if line.startswith("[Ordinal/Name Pointer] Table"):
            in_names = True
        elif in_names:
            m = re.match(r"\s*\[\s*\d+\]\s+(.+)$", line)
            if not m:
                break
            exports.add(m.group(1).split()[-1])
    return exports, imports


def find_dumpbin():
    found = shutil.which("dumpbin")
    if found:
        return found
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    vs = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                        check=True, stdout=subprocess.PIPE, universal_newlines=True).stdout.strip()
    with open(os.path.join(vs, "VC", "Auxiliary", "Build", "Microsoft.VCToolsVersion.default.txt")) as f:
        version = f.read().strip()
    return os.path.join(vs, "VC", "Tools", "MSVC", version, "bin", "Hostx64", "x64", "dumpbin.exe")


def read_dumpbin(dumpbin, dll):
    out = subprocess.run([dumpbin, "/nologo", "/exports", "/dependents", dll], check=True,
                         stdout=subprocess.PIPE, universal_newlines=True).stdout
    # Export lines: ordinal, hint, RVA, name (optionally "= decorated name").
    exports = set(re.findall(r"^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)", out, re.M))
    imports = re.findall(r"^\s+(\S+\.dll)\s*$", out, re.M | re.I)
    return exports, imports


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--objdump", default="objdump", help="objdump to use (default: %(default)s)")
    ap.add_argument("--dumpbin", help="use this dumpbin.exe instead of objdump, or 'auto' to find it")
    ap.add_argument("dlls", nargs="+")
    args = ap.parse_args()

    if args.dumpbin == "auto":
        args.dumpbin = find_dumpbin()
        print("dumpbin: " + args.dumpbin)
    expected = def_exports(DEF_FILE)
    failed = False
    for dll in args.dlls:
        name = os.path.basename(dll)
        if args.dumpbin:
            exports, imports = read_dumpbin(args.dumpbin, dll)
        else:
            exports, imports = read_objdump(args.objdump, dll)

        problems = []
        if exports - expected:
            problems.append("unexpected: " + ", ".join(sorted(exports - expected)))
        if expected - exports:
            problems.append("missing: " + ", ".join(sorted(expected - exports)))
        failed |= bool(problems)
        print("%s exports %s: %s" % ("FAIL" if problems else "PASS", name,
                                       "; ".join(problems) or ", ".join(sorted(exports))))

        bad = [i for i in imports if i.lower() not in SYSTEM_DLLS]
        if not imports:
            bad.append("(no imports found)")
        failed |= bool(bad)
        print("%s imports %s: %s" % ("FAIL" if bad else "PASS", name,
                                       "not allowed: " + ", ".join(bad) if bad else ", ".join(imports)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
