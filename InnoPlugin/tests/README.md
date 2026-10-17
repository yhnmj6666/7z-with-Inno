# Tests

| File | Purpose |
|---|---|
| `run_tests.py` | test runner |
| `make_payload.py` | deterministic payload generator (the test oracle) |
| `corpus/*.iss` | Inno Setup scripts for the test installers |
| `client/InnoClient.cpp` | test client (`inno_client`), built with the plugin |
| `check_dll.py` | checks the exports and imports of the plugin DLLs |

## Running the tests

On Linux, in the agent environment or the CI job (see
`.github/scripts/setup-inno-tools.sh` for the `iscc-<version>` wrappers and
Windows 7-Zip under Wine), build with GCC and MinGW x86_64 as described in
`../README.md`, then run from the repository root:

```
python3 InnoPlugin/tests/run_tests.py
```

The runner:

1. Writes the payload to `build/corpus/payload/` and compiles every script in
   `corpus/` with every compiler (`iscc-5.6.1` (ANSI),
   `iscc-5.6.1-unicode`, `iscc-6.0.5`, `iscc-6.1.2`, `iscc-6.2.2`) into
   `build/corpus/installers/<compiler>/<variant>.exe`. Each installer has a
   `.key` file with a hash of the compiler name, the scripts and
   `make_payload.py`; an installer is compiled again only when its key changes.
   Installers are never committed.
2. Test client (`build/linux/inno_client` with `build/linux/Inno.so`): `i`,
   then `l`, `x` and `t` for every installer. For each compiler's stored
   (`none`) installer, it also flips a byte in `data/random.bin`, extracts with
   `x`, and requires a per-item `crc_error` while all other payload files still
   extract byte-identically.
3. Windows 7-Zip, when a 7-Zip folder and a plugin DLL are given or found
   (default: `/opt/inno-tools/7zip-win64` and
   `build/mingw-x86_64/Inno.64.dll`): the runner copies `7z.exe`, `7z.dll`
   and `7z.sfx` into a private folder with only the plugin in `Formats`, then
   checks that `7z i` loads `Inno` from `Formats`, runs `l`, `x` and `t` for
   every installer, and checks that executables that are not Inno installers
   still open with 7-Zip's own handlers: `7z.exe` lists as `PE`, and a 7z SFX
   (`7z.sfx` plus a `.7z` archive made with `7zz`) lists as `7z`. On Linux,
   7-Zip runs under Wine.

Listings must contain every `[Files]` entry with the payload size, and
extracted files must match the payload byte for byte. The runner maps each
destination (`{app}\docs\manual.txt`) back to its payload file by reading the
`[Files]` section of the scripts, and replaces `:`, `*`, `?`, `<`, `>`, `|`,
`"` and control characters with `_` in each path component, as 7-Zip does on
Windows and the test client does everywhere. The extra items the handler adds
(`unins___.exe` and `embedded\...`) are allowed. Timestamps are not checked
yet.

`t` fails with the current handler (it returns `E_FAIL` in test mode), so it
is reported as `XFAIL`. If it passes, the runner reports `XPASS` and fails:
update the expectation in `run_tests.py` when test mode is fixed.

The runner prints one line per check (`PASS`, `FAIL`, `XFAIL`, `XPASS`,
`SKIP`) and a summary, and exits with 1 if any check failed or passed
unexpectedly.

### Options

| Option | Meaning |
|---|---|
| `--client`, `--plugin` | test client and the plugin it loads |
| `--sevenzip-dir` | Windows 7-Zip folder |
| `--sevenzip-plugin` | DLL to put into a private copy of `--sevenzip-dir`; without it, the plugin must already be in the `Formats` folder of `--sevenzip-dir` |
| `--no-client`, `--no-sevenzip` | skip the client or the 7-Zip checks |
| `--corpus-dir` | corpus folder (default `build/corpus`) |
| `--no-compile` | use the installers in `--corpus-dir` as they are (their `.key` files must match the scripts) |
| `--compilers` | comma-separated compiler labels (default: the five above) |
| `--work-dir` | scratch folder, emptied on start (default `build/test-work`) |

### On Windows

Inno Setup is not needed on Windows: take the corpus from a Linux run (the
CI artifact `inno-corpus`, which contains `payload/` and `installers/`) and
pass it with `--no-compile`. With the plugin copied into the `Formats` folder
of a 7-Zip installation or of an extracted 7-Zip installer:

```
python InnoPlugin\tests\run_tests.py --no-compile --corpus-dir <corpus folder> ^
  --client InnoPlugin\build\x64\Release\inno_client.exe ^
  --plugin InnoPlugin\build\x64\Release\Inno.64.dll ^
  --sevenzip-dir "C:\Program Files\7-Zip"
```

Use the `Win32` build with 32-bit 7-Zip. The handler writes a log file to the
temporary folder on every open (known problem); the runner sets `TMP`, `TEMP`
and `TMPDIR` to `tmp` in the work folder for the programs it starts (under
Wine, 7-Zip keeps Wine's own temporary folder).

## Corpus

`corpus/common.iss` holds the payload and the `[Files]` entries. The variant
scripts cover solid LZMA, non-solid LZMA2, stored (`none`), and both solid and
non-solid ZIP and BZIP2 compression. Each sets the compression options and
includes `common.iss`. The payload (`make_payload.py`) has ASCII names only:

- files in nested folders, an empty file, 3 MiB of pseudo-random data
  (SHA-256 in counter mode) and a compressible text file;
- `shared.txt`, installed to `{app}` and `{app}\copy` (Inno Setup stores the
  data once);
- `code.txt`, installed to `{code:GetDestDir}`, a `[Code]` function.

Keep `[Files]` entries on one line each, with `Source` under `payload\`,
`DestDir`, optionally `DestName`, and no wildcards: the runner parses them.

## Test client

```
inno_client <plugin> i
inno_client <plugin> l <archive> [-p<password>]
inno_client <plugin> t <archive> [-p<password>]
inno_client <plugin> x <archive> <output folder> [-p<password>]
```

Adapted from 7-Zip's `CPP/7zip/UI/Client7z/Client7z.cpp`. It loads the
plugin through the exports 7-Zip uses, supports volumes
(`IArchiveOpenVolumeCallback`) and passwords, and prints one JSON object per
line. Extraction rejects absolute paths and `..`, and replaces characters
that Windows does not allow in file names with `_`. Exit codes: 0 success,
1 usage or plugin error, 2 the archive cannot be opened, 3
`IInArchive::Extract` failed, 4 some items reported errors.

## DLL check

```
python3 InnoPlugin/tests/check_dll.py InnoPlugin/build/mingw-x86_64/Inno.64.dll InnoPlugin/build/mingw-i686/Inno.32.dll
python InnoPlugin\tests\check_dll.py --dumpbin auto InnoPlugin\build\x64\Release\Inno.64.dll
```

Each DLL must export exactly the names in
`third_party/7zip/CPP/7zip/Archive/Archive.def` and import only Windows system
DLLs (`msvcrt.dll` counts as one for MinGW); `VCRUNTIME*`, `MSVCP*`,
`api-ms-win-crt-*` and MinGW runtime DLLs fail the check. It reads the DLLs
with `objdump -p`, or with `dumpbin` (`--dumpbin auto` takes it from `PATH`
or from the latest Visual Studio).
