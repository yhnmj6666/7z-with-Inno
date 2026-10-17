# Inno Setup plugin for 7-Zip

A format plugin that lets stock 7-Zip (7z.exe, 7zG.exe, 7zFM.exe) open, list
and extract Inno Setup installers. 7-Zip loads it from its `Formats` folder,
so 7-Zip updates do not require rebuilding the plugin.

This folder is the standalone rewrite of the plugin in `CPP/7zip/Archive/Inno/`
(see `.github/prompts/inno-plugin-refactor.prompt.md`). Phases 1 and 2 moved
the handler onto a vendored 7-Zip 26.04 subset with a CMake build for MSVC,
GCC and MinGW, and added tests. The handler behaves like the old plugin,
including its known problems (see [Known limitations](#known-limitations)).

## Installation

1. Get `Inno.64.dll` for 64-bit 7-Zip or `Inno.32.dll` for 32-bit 7-Zip (the
   CI workflow uploads both as artifacts, or build them as described below).
2. Create the folder `Formats` in the 7-Zip folder (for example
   `C:\Program Files\7-Zip\Formats\`) and copy the DLL into it. Copy only the
   DLL that matches your 7-Zip, never both. Replace an `Inno.64.dll` or
   `Inno.32.dll` from an older version of the plugin.
3. Check with `7z i`: the `Libs:` section lists the DLL in `Formats`, and the
   `Formats:` section has a line with `Inno` and `exe`.

Then open an installer like any archive, for example `7z l setup.exe` or
`7z x setup.exe -oout`. Paths start with the Inno Setup constant of the
destination, such as `{app}\readme.txt`; 7-Zip on Windows replaces characters
that are not allowed in file names, so `{code:GetDestDir}` is extracted as
`{code_GetDestDir}`. Besides the installed files, the listing contains
`unins___.exe` (empty) and `embedded\` entries (reconstructed install script,
license, language files and wizard images).

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
through Windows 7-Zip. On Windows it runs with a prebuilt corpus and a 7-Zip
folder. See [tests/README.md](tests/README.md). The workflow
`.github/workflows/inno-plugin.yml` builds everything and runs the tests on
Ubuntu (GCC, MinGW, Wine) and Windows (MSVC, official 7-Zip 26.04 x64 and x86).

## Supported Inno Setup versions

The format code comes from innoextract (commit `83c1544b3f`, December 2021).
Tested so far:

| Inno Setup | Tested with |
|---|---|
| 5.6.1 (Unicode), 6.0.5, 6.1.2, 6.2.2 | test corpus (`lzma` solid, `lzma2` non-solid, `none`), every CI run |
| 5.5.0 (Unicode), 6.0.0, 6.2.0 | real installers, checked by the owner with the MSVC build: listing and extraction match the old plugin |

Installers built with `Compression=zip` or `bzip`, encrypted or disk-spanning
installers, and Inno Setup 6.3 or later are not supported yet.

## Known limitations

Inherited from the old plugin; later phases of the rewrite fix them.

- Test mode fails: `7z t`, the Test button and "skip existing files" make
  `Extract()` return `E_FAIL` (7-Zip shows "Call failed"), because the handler
  requires an output stream for every item.
- Progress totals are wrong.
- Names and comments are converted with the ANSI code page instead of UTF-8,
  so non-ASCII paths are garbled.
- The handler writes to the console, writes a log file to the temporary
  folder on every open, and always sets the warning "Check debug log at: ...".
- zlib and bzip2 decompression are stubs: installers built with
  `Compression=zip` or `bzip` cannot be extracted, and old versions with
  zlib-compressed setup headers cannot be opened.
- `IsArc_Inno` always returns NO, and the physical size is not reported.
- The CRC column shows the first 4 bytes of the entry checksum (SHA-1 for the
  tested versions), not a CRC-32.
- The first bad item aborts the whole extraction instead of reporting a
  per-item error. Extraction follows the requested order, so going backwards
  inside a solid chunk restarts decompression, and files that share one data
  location are buffered whole in memory.
- No passwords (encrypted installers), no disk spanning, no Inno Setup 6.3 or
  later.

## Layout

| Path | Contents |
|---|---|
| `CMakeLists.txt`, `cmake/` | build definition, MinGW toolchain files |
| `src/` | handler and 7-Zip glue |
| `src/inno/` | Inno Setup format code derived from innoextract, and `minihead*` (stand-ins for the Boost parts it uses) |
| `third_party/7zip/` | unmodified subset of 7-Zip 26.04, see `third_party/7zip/VERSION.txt` |
| `tests/` | test client, corpus scripts, payload generator, test runner, DLL check |

## License

The plugin contains code under the GNU LGPL 2.1 or later (7-Zip), the zlib
license (innoextract) and the Boost Software License (parts of the `minihead`
headers). See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
