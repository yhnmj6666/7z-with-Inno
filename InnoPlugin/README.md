# Inno Setup plugin for 7-Zip

A format plugin that lets stock 7-Zip (7z.exe, 7zG.exe, 7zFM.exe) open, list,
test and extract Inno Setup installers. 7-Zip loads it from its `Formats`
folder, so 7-Zip updates do not require rebuilding the plugin.

The plugin builds from this folder alone: the Inno Setup format code derived
from innoextract, a vendored subset of 7-Zip 26.04 for the 7-Zip interfaces,
streams and decoders, and a CMake build for MSVC, GCC and MinGW. It replaces
the older plugin that was built inside a 7-Zip 22.00 source tree in this
repository (rewritten as described in
`.github/prompts/inno-plugin-refactor.prompt.md`). See
[Known limitations](#known-limitations).

## Installation

1. Get `Inno.64.dll` for 64-bit 7-Zip or `Inno.32.dll` for 32-bit 7-Zip (the
   CI workflow uploads both as artifacts, or build them as described below).
2. Create the folder `Formats` in the 7-Zip folder (for example
   `C:\Program Files\7-Zip\Formats\`) and copy the DLL into it. Copy only the
   DLL that matches your 7-Zip, never both. Replace an `Inno.64.dll` or
   `Inno.32.dll` from an older version of the plugin.
3. Check with `7z i`: the `Libs:` section lists the DLL in `Formats`, and the
   `Formats:` section has a line with `Inno` and `exe`.

Then open an installer like any archive, for example `7z l setup.exe`,
`7z t setup.exe` or `7z x setup.exe -oout`.

## Items

Every file stored in the installer is listed under its destination path,
which starts with the Inno Setup constant of the destination folder, such as
`{app}\readme.txt`. 7-Zip on Windows replaces characters that are not
allowed in file names, so `{code:GetDestDir}` is extracted as
`{code_GetDestDir}`. Unicode installers store names in UTF-16; for ANSI
installers the plugin uses the code page of the installer's languages
(Windows-1252 if one of them uses it), as innoextract does.

File names are never changed, so the files can be used right after
extraction. Instead, entries get leading folders, in this order:

| Folder | Property |
|---|---|
| `{x86}`, `{x64}`, `{arm64}` | the architecture that the entry's `Check` expression selects, or else the `32bit` or `64bit` flag. The plugin evaluates `Check` expressions made of `Is64BitInstallMode`, `IsWin64`, `IsX64Compatible`, `IsX64`, `IsX64OS`, `IsARM64`, `IsX86`, `IsX86OS`, `IsX86Compatible`, `and`, `or`, `not` and parentheses; an expression that is true on both x64 and Arm64 (such as `Is64BitInstallMode`) gives `{x64}` |
| `{#component}` | a `Components` parameter that is a single component (`\` in subcomponent names becomes `_`) |
| `{@language}` | a `Languages` parameter that is a single language |

If the entries of any destination (ignoring case; on Windows also for
non-ASCII letters) differ in architecture, every entry with an architecture
gets its architecture folder, also when its destination is unique, so that
the files of each architecture stay together. Otherwise no entry gets one.
The component and language folders are added only to entries that have the
same destination as another entry, when that property differs within the
group. An entry without a value for a property gets no folder for it. Entries
whose paths are still not unique then get `{$1}`, `{$2}`, ... in the order of
the entries.

For example, `a.dll` installed to `{app}` once with
`Check: not Is64BitInstallMode` and once with `Check: Is64BitInstallMode` is
listed as `{x86}\{app}\a.dll` and `{x64}\{app}\a.dll`, and a third copy
without a `Check` parameter as `{app}\a.dll`. In the same installer, `b.dll`
installed to `{app}` only once, with `Check: Is64BitInstallMode`, is listed as
`{x64}\{app}\b.dll`.

Besides the installed files, the listing contains files under `embedded\`
that the installer keeps in its setup headers:

| Item | Contents |
|---|---|
| `embedded\install_script.iss` | a script reconstructed from the setup headers: not the original script, approximate (for example `[File]` instead of `[Files]`, no `[Code]`) |
| `embedded\License.txt` or `.rtf` | the license text (`LicenseFile`) |
| `embedded\<language>.isl` | the messages of each language |
| `embedded\<language>.txt` or `.rtf` | the license of each language |
| `embedded\WizardImage<N>.bmp`, `embedded\WizardImageSmall<N>.bmp` | the wizard images (`WizardImageFile`, `WizardSmallImageFile`), named `.png` for PNG images (Inno Setup 6.5.2 and later) |
| `embedded\WizardImageBack<N>`, `embedded\WizardImageDark<N>`, `embedded\WizardImageSmallDark<N>`, `embedded\WizardImageBackDark<N>` | the background images (`WizardBackImageFile`, 6.7 and later) and the images for dark mode (`...DynamicDark`, 6.6 and later), with `.bmp` or `.png` as above |

Modification times are the times Setup gives the installed files. Installers
built with `TimeStampsInUTC=yes` (the default from Inno Setup 7) store UTC.
Otherwise the installer stores the local time of the build computer, and the
plugin converts it to UTC as Setup does on the target computer, with the
current time zone offset of the computer that runs 7-Zip
(`LocalFileTimeToFileTime`).

Archive properties: the methods (for example `LZMA IF5309`: LZMA, and the call
instruction filter of Inno Setup 5.3.9 and later for x86 executables), solid,
physical size, headers size, and a comment with the Inno Setup version, the
application name and version. Warnings from the setup header parser are shown
as archive warnings.

Encrypted installers (`Encryption=yes`, and `Encryption=full` from Inno
Setup 6.5) ask for the password through 7-Zip's password prompt or `-p`:
`Encryption=full` when the installer is opened (its setup headers are
encrypted too), otherwise when the first encrypted file is extracted. The
password is checked against the installer's password test before anything
is decrypted; a wrong one gives "Wrong password" for each encrypted file, or
for `Encryption=full` an empty archive with the error "Headers Error in
encrypted archive. Wrong password?". Inno Setup before 6.4 encrypts with
ARCFOUR (the archive method shows `ARC4`), 6.4 and later with XChaCha20 and
a PBKDF2-SHA256 key (`XChaCha20`). The plugin derives the key with at most
2^24 PBKDF2 iterations (Inno Setup uses 220000), like 7-Zip's 7z and RAR5
handlers; an installer that asks for more gives "Unsupported method" for its
encrypted files, or with `Encryption=full` the error "Unsupported key
derivation".

Installers with a setup data version newer than the plugin knows open as an
empty archive with the error "Unsupported Inno Setup version: <version>"
instead of being declined, so 7-Zip does not show them as a plain `PE` file.

Disk-spanning installers (`DiskSpanning=yes`) keep the file data in slice
files next to the installer: `setup-1.bin`, `setup-2.bin`, ... for
`setup.exe`, or `setup-1a.bin`, `setup-1b.bin`, ... with `SlicesPerDisk`
greater than 1. The plugin opens them through 7-Zip's volume callback, as
7-Zip does for the volumes of multi-volume archives, so open the installer,
not a slice. Like Setup, it looks for the slices under the installer's file
name without its extension, so a renamed installer (for example
`setup (1).exe`) does not find them: rename it back, or rename the slices to
match. Where the setup headers store a base name, that name is tried too
(first for installers from before Inno Setup 4.1.7, which name their slices
after it).
Files whose data starts in a missing slice, or continues into one, are
reported as "Unavailable", and the archive gets the warning
"Missing slice: <name>"; the other files are extracted. A slice that does not
start with a valid slice header gives the warning "Bad slice header: <name>"
and is treated as missing.

7-Zip recognizes installers by their content, so renamed installers (for
example `setup.bin`) open too, and `-tInno` forces the format, for example
`7z l -tInno setup.bin`. Executables that are not Inno Setup installers,
including damaged installers whose setup headers cannot be read, are
declined, so 7-Zip opens them with its other handlers (for example as `PE` or
as a 7z SFX). A setup header block that decompresses to more than 256 MiB is
treated as damaged, so a crafted installer cannot make the plugin use more
memory than that for one block.

The plugin writes nothing to the console or to disk. For debugging, set the
environment variable `INNO_PLUGIN_LOG` to a file name before starting 7-Zip;
the plugin then appends the messages of the setup header parser to that file.

## Building

Requirements: CMake 3.20 or later and a C++17 compiler. Run the commands in
this folder; the build folders are the ones `tests/run_tests.py` uses by
default.

| Toolchain | Configure | Outputs |
|---|---|---|
| GCC (Linux) | `cmake -S . -B build/linux -G Ninja` | `build/linux/Inno.so`, `inno_client` |
| MinGW x86_64 | `cmake -S . -B build/mingw-x86_64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake` | `build/mingw-x86_64/Inno.64.dll`, `inno_client.exe` |
| MinGW i686 | `cmake -S . -B build/mingw-i686 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake` | `build/mingw-i686/Inno.32.dll`, `inno_client.exe` |
| MSVC x64 | `cmake -S . -B build/x64 -A x64` | `build/x64/Release/Inno.64.dll`, `inno_client.exe` |
| MSVC Win32 | `cmake -S . -B build/Win32 -A Win32` | `build/Win32/Release/Inno.32.dll`, `inno_client.exe` |

Then build with `cmake --build <build folder> --config Release`. Visual Studio
2022 can also open this folder directly (CMake project). `-DINNO_BUILD_CLIENT=OFF`
skips the test client.

- MSVC: static CRT (`/MT`), warnings as errors (`/W4 /WX`). The DLLs import
  only Windows system DLLs, so no Visual C++ redistributable is needed.
- GCC and MinGW: `-Wall -Wextra -Werror`. MinGW links the runtime statically;
  the DLLs import only Windows system DLLs and `msvcrt.dll`. The i686 DLL
  exports undecorated names (`--kill-at`).
- Windows builds export exactly the names in
  `third_party/7zip/CPP/7zip/Archive/Archive.def`, like upstream 7-Zip's
  single-format plugins. `Inno.so` is only for the test client.

## Testing

`python3 tests/run_tests.py` compiles the test installers with the Inno Setup
compilers under Wine, then checks the plugin through the test client and
through Windows 7-Zip: listing, methods and timestamps, extraction and test
mode against the payload, a subset in reverse order, skipping existing files,
progress totals, damaged installers (including a setup header block that
decompresses to 1 GiB), disk spanning with all slices and with a missing
slice, other executables, and that the plugin prints nothing and writes no
files. On Windows it runs with a prebuilt corpus
and a 7-Zip folder. See [tests/README.md](tests/README.md). The workflow
`.github/workflows/inno-plugin.yml` builds everything and runs the tests on
Ubuntu (GCC, MinGW, Wine) and Windows (MSVC, official 7-Zip 26.04 x64 and x86).

## Supported Inno Setup versions

The format code comes from innoextract (commit `6e9e34ed08`, February 2025,
for Inno Setup up to 6.4.0.1), extended for the setup data versions of Inno
Setup 6.4.1 to 7.1.0 (the newest is `7.0.0.3`) from the Inno Setup sources.

| Inno Setup | Support | Tested with |
|---|---|---|
| 1.2.10 to 5.0.3 | setup data formats from innoextract (16-bit and 32-bit, ANSI) | not tested |
| 5.0.4 to 6.2.0 | supported | 15 real installers, checked by the owner with the MSVC and MinGW builds: every file has the same content as with the old plugin, and `7z t` passes |
| 5.6.1 (ANSI and Unicode), 6.0.5, 6.1.2, 6.2.2, 6.3.3, 6.4.3, 6.5.4, 6.6.1, 6.7.3, 7.0.2 (x64), 7.1.0 (x64 and x86) | supported | test corpus (`lzma` solid, `lzma2` non-solid, `zip` and `bzip` solid and non-solid, `none`; timestamps in UTC and local time; disk spanning; `Encryption=yes`, and `Encryption=full` from 6.5, with the right, a wrong and no password), every CI run |
| other versions up to 7.1.0 (setup data `7.0.0.3`) | supported | not tested |
| newer setup data versions | opens with the error "Unsupported Inno Setup version" | test with a patched version string |

The encrypted corpus installers of Inno Setup before 6.4 are compiled with the
official `ISCrypt.dll`, which `.github/scripts/setup-inno-tools.sh` downloads
and checks by its SHA-256; where it cannot be downloaded, with a stand-in
(`tests/iscrypt/iscrypt.c`, standard ARCFOUR).

## Known limitations

- Slices of disk-spanning installers are looked up only under the names
  described above (so not for a renamed installer), in the installer's
  folder, with the exact case where the file system is case-sensitive. For an
  installer opened inside another archive (for example in a zip file), 7-Zip
  offers no other volumes, so the files of a disk-spanning installer are
  "Unavailable".
- Wizard images that are neither PNG nor BMP are listed with the extension
  `.bin`.
- Files that Setup downloads (`[Files]` flag `download`, Inno Setup 6.5 and
  later) have no data in the installer and are not listed.
- Timestamps stored in local time are converted with the current time zone
  offset of the computer that runs 7-Zip, not the offset of the build
  computer or of the file's date, as Setup does.
- The CRC column is filled only for entries with a CRC-32 checksum. The
  plugin verifies every checksum type (Adler-32, CRC-32, MD5, SHA-1) when it
  tests or extracts.
- Entries that share one data location are decoded once and kept in memory
  for the other entries up to 64 MiB; larger shared data is decoded again for
  each entry.
- When only some items are extracted, the progress total also counts the data
  decoded to reach them inside a solid chunk.
- A damaged installer reports per-item errors only when its file data is
  damaged. Inno Setup writes the setup headers after the file data, so a
  truncated installer cannot be opened at all.

## Layout

| Path | Contents |
|---|---|
| `CMakeLists.txt`, `cmake/` | build definition, MinGW toolchain files |
| `src/` | handler, data pipeline (`InnoDecoder.*`: chunks through 7-Zip streams and decoders) and 7-Zip glue |
| `src/inno/` | Inno Setup format code derived from innoextract, and `minihead*` (stand-ins for the Boost parts it uses) |
| `third_party/7zip/` | unmodified subset of 7-Zip 26.04, see `third_party/7zip/VERSION.txt` |
| `tests/` | test client, corpus scripts, payload generator, test runner, DLL check |

## License

The plugin contains code under the GNU LGPL 2.1 or later (7-Zip), the zlib
license (innoextract) and the Boost Software License (parts of the `minihead`
headers). See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
