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

1. Writes the payload to `build/corpus/payload/` and compiles every variant
   script in `corpus/` (all but `common.iss` and `timestamps.iss`, listed in
   `VARIANTS` in `run_tests.py`) with every compiler (`iscc-5.6.1` (ANSI),
   `iscc-5.6.1-unicode`, `iscc-6.0.5`, `iscc-6.1.2`, `iscc-6.2.2`), with
   `TZ=UTC`, into `build/corpus/installers/<compiler>/<variant>.exe`. Each
   installer has a `.key` file with a hash of the compiler name, the scripts
   and `make_payload.py`; an installer is compiled again only when its key
   changes. Installers are never committed.
2. Writes damaged copies of some installers to `damaged/` in the work folder:
   `none.exe` with one bit flipped inside `data/random.bin` (`stored`),
   `lzma-solid.exe` with one byte changed in the middle of its solid chunk
   (`solid`), cut in the middle of that chunk (`truncated`), and cut inside
   the setup headers (`headers`).
3. Test client (`build/linux/inno_client` with `build/linux/Inno.so`): `i`,
   then for every installer:
   - `l`: the listing (below), the physical size equals the file size, no
     archive error or warning;
   - the methods: the variant's method, plus `IF5309` for `bin/calls.exe`
     and in the archive method when the data is compressed;
   - the timestamps of the `time-*` variants (below);
   - `x` and `t`: every item reports `ok` once, the files match the payload,
     and the progress total equals the sum of the item sizes, completed
     values never decrease and end at the total;
   - every other item in reverse order (`-i`) with `x` and `t`: only those
     items report results;
   - skipping existing files: after `x`, the first file is changed and one in
     the middle is deleted; `x -s` must extract only the deleted file and
     report all others as skipped.

   Then the damaged copies: `truncated` and `headers` cannot be opened (exit
   code 2); `stored` reports `crc_error` for `data/random.bin` only and
   extracts all other files byte-identically; `solid` reports errors for the
   damaged part of the chunk, and the files that are not affected still
   match. `payload/bin/calls.exe` (a PE file that is no installer) cannot be
   opened.
4. Windows 7-Zip, when a 7-Zip folder and a plugin DLL are given or found
   (default: `/opt/inno-tools/7zip-win64` and
   `build/mingw-x86_64/Inno.64.dll`): the runner copies `7z.exe`, `7z.dll`
   and `7z.sfx` into a private folder with only the plugin in `Formats`, then
   checks that `7z i` loads `Inno` from `Formats`, and for every installer
   `l -slt` (type, physical size, listing, methods), `x` (the files, and the
   file times of the `time-*` variants), `t` and `x -aos` (skip existing
   files, as above). A renamed copy (`lzma-solid.bin`) must open as `Inno`.
   The damaged copies: `truncated` and `headers` must not open as `Inno`
   (7-Zip may open them as `PE`); `t` fails for `stored` and `solid`, and
   `x` of `stored` extracts all files except `data/random.bin`. Executables
   that are not Inno installers still open with 7-Zip's own handlers:
   `7z.exe` lists as `PE`, and a 7z SFX (`7z.sfx` plus a `.7z` archive made
   with `7zz`) lists as `7z`. On Linux, 7-Zip runs under Wine.
5. Checks that the temporary folder of the programs it started is empty: the
   runner sets `TMP`, `TEMP` and `TMPDIR` to `tmp` in the work folder and
   removes `INNO_PLUGIN_LOG` from the environment (under Wine, 7-Zip keeps
   Wine's own temporary folder). The client must also print nothing but
   JSON lines, and nothing to stderr.

Listings must contain every `[Files]` entry with the payload size, under the
item path the handler gives it, and the `embedded\` items: the script, one
`.isl` file per language, and optional wizard images; anything else fails.
Extracted files must match the payload byte for byte, and embedded files
their listed sizes. The runner maps each item path (`{app}\docs\manual.txt`)
back to its payload file by reading the scripts (see [Corpus](#corpus)), and
replaces `:`, `*`, `?`, `<`, `>`, `|`, `"` and control characters with `_` in
each path component, as 7-Zip does on Windows and the test client does
everywhere.

The `time-*` variants are compiled with `TZ=UTC`, so the stored local times
equal the payload times in UTC. `time-utc` must list the payload times.
`time-local` must list them converted with `LocalFileTimeToFileTime`, which
uses the current offset of the time zone: the runner starts the Linux client
with `TZ=America/New_York`, and on Windows (and for 7-Zip under Wine) uses
the system time zone.

The runner prints one line per check (`PASS`, `FAIL`, `SKIP`) and a summary,
and exits with 1 if any check failed.

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

Use the `Win32` build with 32-bit 7-Zip.

## Corpus

`corpus/common.iss` holds the payload and the `[Files]` entries. The variant
scripts cover solid LZMA, non-solid LZMA2, stored (`none`), and both solid and
non-solid ZIP and BZIP2 compression. Each sets the compression options and
includes `common.iss`. `time-utc.iss` and `time-local.iss` include
`timestamps.iss`, which installs two files with fixed modification times
(winter and summer) with `TimeStampsInUTC=yes` and `no`.

The payload (`make_payload.py`) contains:

- files in nested folders, an empty file, 3 MiB of pseudo-random data
  (SHA-256 in counter mode) and a compressible text file;
- `shared.txt`, installed to `{app}` and `{app}\copy` (Inno Setup stores the
  data once);
- `code.txt`, installed to `{code:GetDestDir}`, a `[Code]` function;
- `bin/calls.exe`, a synthetic x86 executable (not a valid program) full of
  `E8`/`E9` call instructions, also at 64 KiB block ends and at the end of
  the file, so the call instruction filter runs when its data is compressed;
- `latin1/` with Windows-1252 names, and `unicode/` with Cyrillic, Greek,
  CJK and emoji names (only in Unicode installers);
- `collide/`, installed to the same destinations with different `Check`
  expressions, flags, components and languages, to test the item names;
- `time/`, a file with a winter and one with a summer modification time
  (every payload file gets a fixed modification time).

The runner reads the scripts to find the expected items, so keep this
format:

- one `[Files]` entry per line, with `Source` under `payload\`, `DestDir`,
  and optionally `DestName`. A `Source` ending in `\*` takes the files in
  that payload folder, with `Flags: recursesubdirs` also those in its
  subfolders;
- `; expect: <item path>` before an entry gives its item path, for entries
  whose destination is not unique;
- the preprocessor directives the runner understands: `#include`, `#define`,
  `#ifdef`, `#ifndef`, `#else` and `#endif` (`UNICODE` is defined for the
  Unicode compilers); others make it fail;
- the languages in `[Languages]` (`default` without that section) give the
  expected `embedded\<name>.isl` items.

## Test client

```
inno_client <plugin> i
inno_client <plugin> l <archive> [-p<password>]
inno_client <plugin> t <archive> [-p<password>] [-i<index>[,<index>...]]
inno_client <plugin> x <archive> <output folder> [-p<password>] [-i<index>[,<index>...]] [-s]
```

`-i` tests or extracts only the given items, and passes the indices to the
plugin in the given order. `-s` skips items whose output file exists (like
7-Zip's `-aos`).

Adapted from 7-Zip's `CPP/7zip/UI/Client7z/Client7z.cpp`. It loads the
plugin through the exports 7-Zip uses, supports volumes
(`IArchiveOpenVolumeCallback`) and passwords, and prints one JSON object per
line: `format` (`i`), `archive` and `item` (`l`), `item_result` and `skipped`
for each item, and a final `extract_result` (`t` and `x`) with the numbers
of items, errors and skipped items and the progress `total`, the last
`completed` value and whether it ever decreased (`completed_decreased`).
Extraction rejects absolute paths and `..`, and replaces characters that
Windows does not allow in file names with `_`. Exit codes: 0 success, 1
usage or plugin error, 2 the archive cannot be opened, 3
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
