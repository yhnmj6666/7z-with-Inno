# Tests

| File | Purpose |
|---|---|
| `run_tests.py` | test runner |
| `make_payload.py` | deterministic payload generator (the test oracle) |
| `corpus/*.iss` | Inno Setup scripts for the test installers |
| `old-versions.txt` | the installers of the old Inno Setup versions, with their SHA-256 |
| `client/InnoClient.cpp` | test client (`inno_client`), built with the plugin |
| `check_dll.py` | checks the exports and imports of the plugin DLLs |

## Running the tests

On Linux, in the agent environment or the CI jobs (see
`.github/scripts/setup-inno-tools.sh` for the `iscc-<version>` wrappers, the
old Inno Setup versions in `/opt/inno-tools/old` and Windows 7-Zip under
Wine), build with GCC and MinGW x86_64 as described in `../README.md`, then
run from the repository root:

```
python3 InnoPlugin/tests/run_tests.py
```

The runner:

1. Writes the payload to `build/corpus/payload/` and compiles every variant
   script in `corpus/` (listed in `VARIANTS` in `run_tests.py`; the others
   are included by them) with every compiler (`COMPILERS`: `iscc-5.6.1`
   (ANSI), `iscc-5.6.1-unicode`, `iscc-6.0.5` to `iscc-6.7.3`,
   `iscc-7.0.2-x64`, `iscc-7.1.0-x64` and `iscc-7.1.0-x86`), and the
   encrypted variants (`ENCRYPTED_VARIANTS`: `encrypted.iss` with
   `Encryption=yes` from 4.2.7, `encrypted-full.iss` with `Encryption=full`
   from 6.5, password `inno-test-password`), with
   `TZ=UTC`, into `build/corpus/installers/<compiler>/<variant>.exe`
   (`codepage.iss`, `CODEPAGE_VARIANTS`, only with the ANSI compilers). The
   disk-spanning variant (`SPANNED_VARIANTS`: `spanned.iss`, with
   `DiskSliceSize` and the extra file `spanned/big.bin`) also writes the
   slices `spanned-1.bin`, `spanned-2.bin`, ... (`spanned.1`, ... before
   Inno Setup 2.0; at least 3). Each
   installer has a `.key` file with a hash of the compiler name, the scripts
   and `make_payload.py`; an installer is compiled again only when its key
   changes. Installers are never committed.

   The old compilers (`OLD_COMPILERS`: `iscc-1.3.26`, `iscc-2.0.19`,
   `iscc-3.0.7`, `iscc-4.0.8`, `iscc-4.0.11`, `iscc-4.1.5`, `iscc-4.1.8`,
   `iscc-4.2.7`, `iscc-5.1.14`, `iscc-5.2.3`, `iscc-5.3.8` and
   `iscc-5.5.9`, the last two also `-unicode`) have no preprocessor, and
   lack many script features. For them the runner applies the preprocessor
   lines itself (see [Corpus](#corpus)) and writes the plain script to
   `installers/<compiler>/<variant>.iss`, which it compiles without command
   line switches. It builds only the variants whose features the compiler
   has (`VARIANT_FEATURES`; for example LZMA from 4.1.5, `TimeStampRounding=0`
   for the `time-*` variants from 4.1.5, LZMA2 from 5.3.9).

   It also copies the installers of the old Inno Setup versions
   (`old-versions.txt`; 1.2.16 to 5.6.1, downloaded by
   `setup-inno-tools.sh old` to `--old-dir`) to `build/corpus/old/`, and
   writes what innoextract extracts from each (`--dump --collisions
   rename-all`: path, size and SHA-256 of every file) to `<name>.json`.
2. Writes damaged copies of some installers of the `COMPILERS` to `damaged/`
   in the work folder:
   `none.exe` with one bit flipped inside `data/random.bin` (`stored`),
   `lzma-solid.exe` with one byte changed in the middle of its solid chunk
   (`solid`), cut in the middle of that chunk (`truncated`), and cut inside
   the setup headers (`headers`), and with a setup header block replaced by
   one that decompresses to 1 GiB of zeros (`bomb`).
3. Test client (`build/linux/inno_client` with `build/linux/Inno.so`): `i`,
   then for every installer:
   - `l`: the listing (below), the physical size equals the file size, no
     archive error or warning;
   - the methods: the method of the variant's `Compression` directive (ZIP
     before Inno Setup 2.0, and for `none` before 4.2.5, which stores zlib
     streams), plus the call instruction filter of the compiler for
     `bin/calls.exe` and in the archive method when the data is compressed:
     `IF4108` from 4.1.8, `IF5200` from 5.2.0, `IF5309` from 5.3.9;
   - the timestamps of the `time-*` variants (below);
   - `x` and `t`: every item reports `ok` once, the files match the payload,
     and the progress total equals the sum of the item sizes, completed
     values never decrease and end at the total;
   - every other item in reverse order (`-i`) with `x` and `t`: only those
     items report results;
   - skipping existing files: after `x`, the first file is changed and one in
     the middle is deleted; `x -s` must extract only the deleted file and
     report all others as skipped.

   Then the damaged copies: `truncated`, `headers` and `bomb` cannot be
   opened (exit code 2; for `bomb` the client must use less than 768 MiB of
   memory); `stored` reports `crc_error` for `data/random.bin` only and
   extracts all other files byte-identically; `solid` reports errors for the
   damaged part of the chunk, and the files that are not affected still
   match. `payload/bin/calls.exe` (a PE file that is no installer) cannot be
   opened. A copy of the spanned installer without its middle slice lists
   all items with the warning `Missing slice: <name>`, extracts the files
   before that slice byte-identically and reports the others as
   `unavailable`.

   Then the encrypted installers: with the right password, `l` lists the
   expected items (the files with data are encrypted, the archive method
   names `ARC4` or `XChaCha20`) and `t` passes. With a wrong password, every
   file with data reports `wrong_password` and the others are `ok`; for
   `Encryption=full`, `l` lists no items and reports the
   `EncryptedHeadersError` flag. Without a password the client's callback
   cancels (`E_ABORT`). Before 6.4, `Encryption=yes` needs
   `ISCrypt.dll` in the compiler folder; `.github/scripts/setup-inno-tools.sh`
   installs the official DLL, checked by its SHA-256, or a stand-in built from
   `iscrypt/iscrypt.c` when the download fails; without either, these
   installers are skipped.

   Then two copies with an unknown newer setup data version (`UNSUPPORTED`,
   written by `make_unsupported` to the work folder), each only when an
   installer of its compilers is in the run (`lzma-solid` if there is one):
   a `7.1.0-x64` or `7.1.0-x86` installer with `(7.0.0.3)` changed to
   `(9.9.9.9)` must list the same items as the original, with the archive
   warning `Unknown Inno Setup version 9.9.9.9, read as 7.0.0.3`, no error
   and `9.9.9.9` in the comment, and pass `t`; a `5.6.1` installer with
   `(5.5.7)` changed to `(9.9.9)`, which the newest format does not fit,
   must open without items, with the error "Unsupported Inno Setup version:
   9.9.9" and the `UnsupportedFeature` error flag.

   Then the old installers: `l` (no archive error or warning, no item with an
   empty path, `embedded\uninstall.exe` before 5.0, the physical size), `t`
   (every item `ok`, every stored checksum verified), and `x`, whose files
   must equal what innoextract extracts: for every destination (the path
   without the leading folders for the same destination and without
   innoextract's renaming) the same number of files with the same contents,
   and leading folders only where innoextract renames too. Items under
   `embedded\` are not compared.
4. Windows 7-Zip, when a 7-Zip folder and a plugin DLL are given or found
   (default: `/opt/inno-tools/7zip-win64` and
   `build/mingw-x86_64/Inno.64.dll`): the runner copies `7z.exe`, `7z.dll`
   and `7z.sfx` into a private folder with only the plugin in `Formats`, then
   checks that `7z i` loads `Inno` from `Formats`, and for every installer
   `l -slt` (type, physical size, listing, methods), `x` (the files, and the
   file times of the `time-*` variants), `t` and `x -aos` (skip existing
   files, as above). A renamed copy (`lzma-solid.bin`) must open as `Inno`.
   The damaged copies: `truncated`, `headers` and `bomb` must not open as `Inno`
   (7-Zip may open them as `PE`); `t` fails for `stored` and `solid`, and
   `x` of `stored` extracts all files except `data/random.bin`. Executables
   that are not Inno installers still open with 7-Zip's own handlers:
   `7z.exe` lists as `PE`, and a 7z SFX (`7z.sfx` plus a `.7z` archive made
   with `7zz`) lists as `7z`. The copies with a newer setup data version:
   `9.9.9.9` lists the items of the original and prints the warning, without
   errors, and `t` passes; `l` of `9.9.9` exits with code 2 and prints
   "Unsupported Inno Setup version". The old installers: `l -slt` (no
   warning, and no item named after the archive), `t` and `x` (compared with
   innoextract, as above). On Linux, 7-Zip runs under Wine.
5. Checks that the temporary folder of the programs it started is empty: the
   runner sets `TMP`, `TEMP` and `TMPDIR` to `tmp` in the work folder and
   removes `INNO_PLUGIN_LOG` from the environment (under Wine, 7-Zip keeps
   Wine's own temporary folder). The client must also print nothing but
   JSON lines, and nothing to stderr.

Listings must contain every `[Files]` entry with the payload size, under the
item path the handler gives it, and the `embedded\` items: the script, one
`.isl` file per language, optional wizard images, and Setup's uninstaller
`embedded\uninstall.exe` before Inno Setup 5.0; anything else fails.
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

The runner prints one line per check (`PASS`, `FAIL`, `SKIP`, `XFAIL`) and a
summary, and exits with 1 if any check failed.

### Known issues

`KNOWN_ISSUES` in `run_tests.py` lists the plugin problems that the tests
show (see the Known limitations in `../README.md`); `installer_issues` and
`old_issues` say which installers have them. A check of an installer that
has a known issue reports `XFAIL` while the problem shows; once it no longer
shows, the check fails until the issue is removed from `KNOWN_ISSUES` and
the README. There are none at present: the three that the old versions
showed (the uninstaller listed with an empty path, code pages other than
1252, and the slice names of Inno Setup 1.3) are fixed.

### Options

| Option | Meaning |
|---|---|
| `--client`, `--plugin` | test client and the plugin it loads |
| `--sevenzip-dir` | Windows 7-Zip folder |
| `--sevenzip-plugin` | DLL to put into a private copy of `--sevenzip-dir`; without it, the plugin must already be in the `Formats` folder of `--sevenzip-dir` |
| `--no-client`, `--no-sevenzip` | skip the client or the 7-Zip checks |
| `--corpus-dir` | corpus folder (default `build/corpus`) |
| `--no-compile` | use the installers in `--corpus-dir` as they are (their `.key` files must match the scripts) |
| `--compile-only` | only compile the installers that are missing or out of date in `--corpus-dir`, and prepare the old installers, then stop (the CI corpus job) |
| `--compilers` | comma-separated compiler labels; `modern` and `old` stand for all of `COMPILERS` and of `OLD_COMPILERS` (default `modern,old`) |
| `--old-installers` | old installers to test: `all` (default), `none`, or comma-separated major versions such as `1,2,3,4` |
| `--old-dir` | the downloaded old installers, to prepare the corpus (default `/opt/inno-tools/old` on Linux) |
| `--work-dir` | scratch folder, emptied on start (default `build/test-work`) |

### On Windows

Inno Setup is not needed on Windows: take the corpus from a Linux run (the
CI artifact `inno-corpus`, which contains `payload/`, `installers/` and
`old/`) and pass it with `--no-compile`. With the plugin copied into the
`Formats` folder of a 7-Zip installation or of an extracted 7-Zip installer:

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
includes `common.iss`; `spanned.iss`, `encrypted.iss` and `codepage.iss` take
the best method of the compiler from `compression.iss` (LZMA2, or LZMA or ZIP
for old compilers). `codepage.iss` has a single language with
`LanguageCodePage=1251`, so the setup headers use code page 1251, and
installs `latin1/`: the compiler stores the Windows-1252 bytes of those
names (Wine's ANSI code page), so the expected item names are their meaning
in code page 1251 (for example the byte 0xE9, e acute in Windows-1252, is
listed as the Cyrillic short i). `time-utc.iss` and `time-local.iss` include `timestamps.iss`,
which installs two files with fixed modification times (winter and summer)
with `TimeStampsInUTC=yes` and `no`.

For the old compilers the runner defines `SMALL_CORPUS`, which leaves
`data/random.bin` out, and `NO_<feature>` for every feature in `FEATURES`
that the compiler lacks: `COMPRESSION` (before 2.0 only zlib, without the
`Compression` directive), `COMPONENTS`, `RECURSE`, `SOLID`, `SLICES` (before
4.0 disk spanning uses `DiskSize`), `CODE`, `LANGUAGES`, `UTC`, `LZMA`,
`ROUNDING`, `GERMAN`, `ENCRYPTION`, `CHECK` (the `Check` functions for 64-bit
installs and the `32bit` and `64bit` flags), `CODEPAGE` (`[LangOptions]
LanguageCodePage`), `LZMA2` and `ARM64`. The scripts leave out or replace
what the compiler cannot compile. In the plain script it adds to `[Setup]`
the folders, `OutputBaseFilename` and `AppVerName` (required by the old
versions).

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
  expressions, flags, components and languages, and one file with a unique
  destination and `Check: Is64BitInstallMode` (listed under `{x64}\`), to
  test the item names;
- `time/`, a file with a winter and one with a summer modification time
  (every payload file gets a fixed modification time).

The runner reads the scripts to find the expected items, so keep this
format:

- one `[Files]` entry per line, with `Source` under `payload\`, `DestDir`,
  and optionally `DestName`. A `Source` ending in `\*` takes the files in
  that payload folder, with `Flags: recursesubdirs` also those in its
  subfolders;
- `; expect: <item path>` before an entry gives its item path, for entries
  listed with leading folders (destinations that are not unique, and
  entries with an architecture);
- the preprocessor directives the runner understands: `#include`,
  `#define` (with a quoted value or none), `#ifdef`, `#ifndef`, `#else` and
  `#endif`, and `{#name}` outside comments (`UNICODE` is defined for the
  Unicode compilers); others make it fail;
- the languages in `[Languages]` (`default` without that section, none
  before Inno Setup 2.0) give the expected `embedded\<name>.isl` items.

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
Extraction rejects absolute paths, `..` and empty paths, and replaces
characters that Windows does not allow in file names with `_`. Exit codes: 0
success, 1 usage or plugin error, 2 the archive cannot be opened, 3
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
