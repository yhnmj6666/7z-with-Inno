# Rewrite the Inno Setup plugin for 7-Zip

Prompt for the GitHub Copilot cloud agent. Start each task with:

> Implement Phase N of `.github/prompts/inno-plugin-refactor.prompt.md`.

Work on exactly one phase per task and pull request, based on the `main` branch (the branch `original` is an unrelated mirror of plain 7-Zip releases). Read the whole file before starting; sections 1 to 7 apply to every phase.

Owner prerequisites: the repository default branch must contain `.github/workflows/copilot-setup-steps.yml`, otherwise the agent environment in section 6 is not installed; select `main` as the base branch when starting a task; approve workflow runs on agent pull requests when asked.

## 1. Goal

Open, list, test and extract Inno Setup installers in stock 7-Zip (7z.exe, 7zG.exe, 7zFM.exe) through a format plugin DLL that users copy into `<7-Zip folder>\Formats\`. 7-Zip updates must never require rebuilding the plugin. The current code works but is a quick port with known defects; rewrite it into a maintainable, well-tested plugin.

## 2. Current state

- Phases 1 and 2 are done (#1): `InnoPlugin/` holds the standalone plugin (vendored 7-Zip 26.04, CMake build, test client, corpus, CI), with the handler in `InnoPlugin/src/` and the innoextract port in `InnoPlugin/src/inno/`. Later phases change this code; of the problems in section 3, Phase 3 fixed 1 and 2, Phase 4 fixed 3 to 9, Phase 5 fixed 10 except disk spanning, and Phase 6 fixed the rest. The original code described below was removed from `CPP/` in Phase 6; it remains in the git history (for example the `22.00` tag plus the commits before Phase 6) as the parity reference.
- Phase 3 is done (#2): headers are decompressed into memory, and file data goes through 7-Zip streams and decoders (`InnoPlugin/src/InnoDecoder.*`, Deflate/zlib and BZip2 vendored from 7-Zip 26.04). `minihead_iostreams.hpp` is gone; checksum mismatches are reported as CRC errors, and encrypted files as unsupported until Phase 5. The rewritten call-instruction filter matches innoextract.
- Phase 4 is done (#3): the handler behaves like 7-Zip's own formats (test mode, skipped items, per-item results, extraction in data order with a 64 MiB cache for shared data, progress, cancellation, UTF-8 names, `kpidPhySize`, `IsArc_Inno` in `InnoPlugin/src/InnoPe.cpp`), writes nothing to the console or to disk (`INNO_PLUGIN_LOG` enables a debug log), names items as described in section 4, and converts local timestamps. The corpus covers the call-instruction filter (a synthetic executable, checked through `kpidMethod`), Unicode names, collisions, timestamps and damaged installers, and the test client has `-i` (subset) and `-s` (skip existing).
- Phase 5 is done (#4): the innoextract port is at `6e9e34e`, and setup data 6.4.1 to 7.0.0.3 (Inno Setup 6.4.1 to 7.1.0) comes from the Inno Setup sources. Encrypted installers open with a password: ARC4, XChaCha20 with a PBKDF2-SHA256 key (at most 2^24 iterations), and `Encryption=full`. Setup data versions newer than the plugin knew opened with an "Unsupported Inno Setup version" error until Phase 8. The corpus covers the 13 compilers of section 6 with encrypted variants; the setup installs the official `ISCrypt.dll` for the compilers before 6.4, with `InnoPlugin/tests/iscrypt/iscrypt.c` as the fallback.
- Phase 6 is done (#5): disk spanning reads the slices through `IArchiveOpenVolumeCallback` (spanned corpus variant, missing-slice test), setup header blocks are limited to 256 MiB decompressed (header bomb test), wizard images are named by content (PNG/BMP) with the back and dark sets listed, unused innoextract files are removed, and the old integration below (`CPP/7zip/Archive/Inno/`, `CPP/7zip/Bundles/InnoPlugin/`, the `Format7z.vcxproj` entries and the two hacks) is gone. `InnoPlugin/README.md` is the plugin's documentation.
- After Phase 6 the 7-Zip 22.00 sources (`Asm/`, `C/`, `CPP/`, `DOC/`) were removed as well: the repository holds `InnoPlugin/`, `.github/`, `README.md`, `COPYING` and `.gitignore`. The release tags `15.05` to `22.00` and the earlier commits keep the 7-Zip history.
- Phase 7 is done (#6): the old Inno Setup versions are tested in CI. `InnoPlugin/tests/old-versions.txt` lists the installers of all 124 old versions on https://files.jrsoftware.org/is/ (1.2.16 to 5.6.1) with their SHA-256; `setup-inno-tools.sh old` downloads them and unpacks 14 old compilers (1.3.26 to 5.5.9). The runner tests every old installer against what innoextract extracts, compiles the variants each old compiler supports from the same corpus scripts (it applies the preprocessor lines itself, see `FEATURES`), checks the call-instruction filters `IF4108` and `IF5200`, and reported the three plugin problems that Phase 8 fixed as expected failures (`KNOWN_ISSUES`, now empty).
- Phase 8 is done (#7): before Inno Setup 5.0, Setup's uninstaller and the regserver helper are listed as `embedded\uninstall.exe` and `embedded\regsvr.exe`. ANSI installers are converted with `MultiByteToWideChar` on Windows and iconv on Linux: file names and setup texts in one code page per installer, language names and custom messages in their language's code page (the `codepage` corpus variant covers it). Inno Setup 1.3 slices (`<name>.1`, ...) are found. Setup data versions newer than 7.0.0.3 are read with the 7.0.0.3 format and a warning; the error "Unsupported Inno Setup version" remains for installers that format does not fit. The runner prints UTF-8 on any console.
- The original code (removed in Phase 6): this repository was a git copy of the 7-Zip 22.00 source tree with an Inno Setup handler added:
  - `CPP/7zip/Archive/Inno/` - 7-Zip glue: `InnoHandler.cpp` (IInArchive), `InnoIn.*` (CInArchive, CEmbeddedFiles), `InnoInterop.*` (std::streambuf over IInStream, progress writer), `InnoItem.*`, `InnoRegister.cpp` (`REGISTER_ARC_I_NO_SIG("Inno", "exe", 0, 0x70, ...)`, flags `kPureStartOpen | kUseGlobalOffset`).
  - `CPP/7zip/Archive/Inno/InnoExtract/` - partial port of innoextract (https://github.com/dscharrer/innoextract, zlib license), copied in 2022 from master at commit `83c1544b3f` (2021-12-02, newer than release 1.9); the owner later applied the equivalent of upstream fix `325a7c9655` to `crypto/iteratedhash.hpp`. Boost is replaced by shims written for this repository (`minihead.hpp`, `minihead_iostreams.hpp`); LZMA comes from prebuilt MSVC liblzma libraries (`liblzma/lib/*.lib`, DLL CRT). `cli/` is not compiled, and `lib/magic_enum.hpp` is included by `InnoIn.cpp` but unused.
  - `CPP/7zip/Bundles/InnoPlugin/InnoPlugin.vcxproj` - builds the plugin (`Inno.64.dll`, `Inno.32.dll`), MSVC 2022 only, against this repository's 7-Zip 22.00 sources. See the "Inno Setup plugin" section of `README.md`.
- The owner verifies each phase locally in the official 7-Zip 26.04 x64 and x86, against the previous build, with 15 real installers (Inno Setup 5.0.4 to 6.2, signed and unsigned), Inno Setup's own installers 5.6.1 to 7.1.0, and encrypted installers built with the real compilers (ARC4 with the official `ISCrypt.dll`, XChaCha20, `Encryption=full`, a non-ASCII password). File contents must stay identical and `7z t` must pass. Item paths, times and the CRC column differ from the original plugin by design since Phase 4. The 15 real installers are third-party software and must never be added to this public repository.
- How 7-Zip picks the handler: 7-Zip sorts formats by name (case-sensitive) and offers a file to every format registered for its extension, in that order. `Inno` is registered for `exe` and sorts before `PE`, so it gets the first try on every `.exe`, including signed installers that `PE` would otherwise claim. Consequences: keep the format name `Inno`, and make `Open()` reject non-Inno executables quickly and silently with `S_FALSE`.
- The older "built into 7z.dll" integration (`CPP/7zip/Bundles/Format7zF/Format7z.vcxproj`, plus two hacks in the 7-Zip tree: `CPP/7zip/Archive/IArchive.h` flipped `#ifdef _MSC_VER` to `#ifndef` around `MY_NO_THROW_DECL_ONLY`, and `CPP/7zip/UI/FileManager/resource.rc` commented out the manifest) was retired in Phase 6: those files are back to 7-Zip 22.00.

## 3. Known problems

1. Native codecs: LZMA uses prebuilt liblzma binaries, which also force the DLL CRT (users need the VC++ redistributable). 7-Zip has native LZMA, LZMA2, Deflate/zlib and BZip2 decoders.
2. `zlib_decompressor` and `bzip2_decompressor` in `minihead_iostreams.hpp` are unfinished stubs that return no data. Installers built with `Compression=zip` or `bzip` cannot be extracted, and older versions whose setup headers are zlib-compressed (`stream/block.cpp`) cannot even be opened.
3. Progress is broken: `Extract()` computes `SetTotal` with `for (UInt32 i = numItems - 1; i > 0; i--)` (skips the first item), compares an archive index with a count (`firstAppearance < numItems`), and `InteropFileStreamProgressWriter` runs a new `CBufInStream` plus `CCopyCoder` for every buffer.
4. Test mode and skipped items fail: `Extract()` returns `E_FAIL` when `GetStream()` returns no stream (`7z t`, the GUI Test button, "skip existing files").
5. Error handling: exceptions are only contained at the COM boundary (function-try-blocks in `Open()` and `Extract()`), so the first bad item aborts the whole extraction instead of reporting a per-item result.
6. Unicode: innoextract stores names as UTF-8 (`util::encoded_string`, `setup/file_setup.cpp`), but the handler converts them with `MultiByteToUnicodeString` (ANSI code page), so non-ASCII paths are garbled. Comments have the same problem.
7. Performance: `InteropStream` is unbuffered (`underflow()` reads one byte and seeks back); extraction follows the requested index order, so going backwards inside a solid chunk restarts decompression (`ResetChunkReader`); every `GetProperty()` call allocates a `shared_ptr` item; file entries that share one data location are buffered whole in `std::string` (unbounded memory).
8. Detection: `IsArc_Inno` always returns `k_IsArc_Res_NO`; `kpidPhySize` is not reported.
9. Side effects: the innoextract logger prints to `std::cout`/`std::cerr` (garbles 7z.exe output) and writes `%TEMP%\<name>_<time>.log` on every open; `kpidWarning` is always "Check debug log at: ..." (debugging leftover); `OpenInfo()` prints parse errors to `std::cout`.
10. Missing features: encrypted installers (ARC4 code exists, but no password is ever obtained), disk-spanning installers (`PrepareExtraction()` throws `NotImplemented`, surfaced as `E_NOTIMPL`), and Inno Setup 6.3 or later (the version table ends at 6.1.0).
11. Minor: MSVC-only constructs (`__FUNCSIG__`, `#pragma warning`), deprecated `std::wstring_convert`, C4996 suppressions, a reimplemented subset of Boost, dead code.

## 4. Target design

### Layout

A self-contained folder at the repository root (suggested; justify deviations in the PR):

```
InnoPlugin/
  README.md                  build, install, usage, supported versions, known limitations
  THIRD-PARTY-NOTICES.md     7-Zip (LGPL) and innoextract (zlib) notices
  CMakeLists.txt             single build definition: MSVC, GCC, MinGW
  third_party/7zip/          unmodified subset of upstream 7-Zip 26.04, upstream paths (C/, CPP/)
  third_party/7zip/VERSION.txt   tag, commit and how the subset was selected
  src/                       handler and 7-Zip glue
  src/inno/                  Inno Setup format code (derived from innoextract and the Inno Setup sources)
  tests/                     corpus scripts, payload generator, test client, test runner
```

- Vendor only the 7-Zip files the plugin needs, copied byte-for-byte from upstream tag `26.04` (https://github.com/ip7z/7zip, local clone `/opt/src/7zip`). Never edit vendored files; updating 7-Zip means copying newer files. Use the current interface macros (`Z7_COM7F_IMF`, `Z7_IFACE_COM7_IMP`, ...), so no `IArchive.h` hack is needed.
- Exports: follow upstream's single-format plugin recipe, `CPP/7zip/Archive/7z/makefile` (builds a `7z.dll` plugin): `Archive/ArchiveExports.cpp`, `Archive/DllExports2.cpp` and `Compress/CodecExports.cpp` (which defines `GetModuleProp`), with `Archive/Archive.def`. The archive-only `Archive/DllExports.cpp` does not define `GetModuleProp` or `SetLargePageMode2`, which `Archive.def` exports. Keep the format name `Inno`, extension `exe` and flags `kPureStartOpen | kUseGlobalOffset`, plus the 7-Zip class ID with archive id `0x70`, unless there is a documented reason to change them.

### Architecture

Model the handler on 7-Zip's NSIS handler (`CPP/7zip/Archive/Nsis/`), which solves the same problem: installer data appended to a PE stub, solid and non-solid streams, decoded with 7-Zip codecs.

- Loader: find the setup loader offset table (`rDlPtS` RCDATA resource 11111, or the legacy `Inno` marker at offset 0x30), as in innoextract `loader/offsets.cpp`.
- Setup headers: decode the CRC-protected block stream with 7-Zip decoders into memory, then parse with the innoextract-derived parser (it encodes many version quirks; keep that knowledge, but feed it from a memory buffer instead of the stream-chain shims).
- File data: read chunks through 7-Zip streams (`IInStream`, `ISequentialInStream`, limited streams), decrypt when needed, decompress with 7-Zip decoders (stored; zlib via `NCompress::NZlib::CDecoder`; BZip2 via `NCompress::NBZip2::CDecoder`; LZMA1 with Inno's 5-byte properties header via `C/LzmaDec.c`; LZMA2 via `C/Lzma2Dec.c`), apply Inno's executable call filter (innoextract `stream/exefilter.hpp`), and verify checksums (Adler-32, CRC-32 via `C/7zCrc.c`, MD5/SHA-1/SHA-256 via `C/Md5.c`, `C/Sha1.c`, `C/Sha256.c`).
- No `std::iostream` in the file data path, no Boost shims, no prebuilt binaries, no global mutable state.

### Format coverage

- Everything innoextract master supports (pinned commit `6e9e34ed0876014fdb46e684103ef8c3605e382e`, local clone `/opt/src/innoextract`). Its version table ends at 6.4.0.1 and it adds XChaCha20 and PBKDF2-SHA256 encryption, but in the environment smoke test it reads installers from 6.3.3 and earlier only; 6.4.3 already fails.
- Inno Setup 6.4.1 and later (6.4.x, 6.5.x, 6.6.x, 6.7.x, 7.0.x, 7.1.x) are not readable by any innoextract version. Derive their changes from the Inno Setup sources (https://github.com/jrsoftware/issrc, local clone `/opt/src/issrc`, tags `is-6_0_5` to `is-7_1_0`): `Projects/Struct.pas` (6.0.x) and `Projects/Src/Shared.Struct.pas` (6.3 and later) define the records, together with `Projects/Src/Shared.EncryptionFunc.pas`, `Projects/Src/Setup.FileExtractor.pas`, the `Projects/Src/Compression.*` units and `Projects/Res/SetupLdr.offsettable.txt`. Compare the files between tags to find each format change.
- Unknown future versions must fail with a clear archive error, never crash or extract garbage.

### Behavior in 7-Zip

- Listing: `kpidPath` (UTF-16 converted from UTF-8; Inno constants such as `{app}` stay in the path, after any collision folders), `kpidSize`, `kpidMTime` (UTC; entries without the `TimeStampInUTC` flag hold the build machine's local time, which Setup restores as local time, so convert them with `LocalFileTimeToFileTime`), `kpidAttrib`, `kpidEncrypted`, `kpidMethod`, `kpidCRC` (only when the entry checksum is CRC-32), `kpidSolid`. Archive properties: `kpidMethod`, `kpidSolid`, `kpidPhySize`, `kpidHeadersSize`, `kpidComment` (Inno Setup version, app name and version), `kpidError`/`kpidWarning` only for real problems.
- File names are never changed, so extracted files can be used as they are. Entries whose destinations collide (ignoring case) are separated by leading folders, in this order: architecture `{x86}`, `{x64}` or `{arm64}` (from simple Check expressions such as `Is64BitInstallMode` or `IsWin64 and not IsARM64`, otherwise from the `32bit`/`64bit` flags), component `{#name}`, language `{@name}`, and `{$1}`, `{$2}`, ... for entries that still collide (the first keeps its path). Component and language folders are added only where they differ within a colliding group. Once any colliding group differs in architecture, every entry with an architecture gets its architecture folder, also when its destination is unique, so each architecture's files stay together. Document the scheme.
- Keep the generated extras under `embedded\` (reconstructed script, licenses, language files, wizard images) and document them.
- Extraction: decode in data order (sort the requested items by chunk and offset, decode each solid chunk at most once per call) while reporting results for the requested indices; support test mode and items without an output stream; report per-item results (`kOK`, `kDataError`, `kCRCError`, `kUnsupportedMethod`, `kWrongPassword`, `kUnavailable`, ...); honor `E_ABORT` from callbacks; never let a C++ exception cross the COM boundary.
- Progress: `SetTotal`/`SetCompleted` in unpacked bytes, including bytes decoded only to reach a later item in a solid chunk; packed-size progress through `ICompressProgressInfo` so the ratio is meaningful.
- Shared data: when several entries use one data location, decode it once and write it to every requested item without unbounded buffering.
- Passwords: obtain them through `ICryptoGetTextPassword` from the open or extract callback, check them against the header password hash before decrypting, and report `kWrongPassword` on mismatch.
- Disk spanning: open `<base>-1.bin`, `<base>-1a.bin`, ... through `IArchiveOpenVolumeCallback` (naming schemes as in innoextract `stream/slice.cpp`); a missing slice reports `kUnavailable` for the affected items only.
- Detection: implement `IsArc_Inno` so `-t#`, "Open Inside #" and renamed installers work. It sees only a prefix of the file and must not do I/O: return `k_IsArc_Res_YES` when the Inno loader marker is inside the buffer, `k_IsArc_Res_NEED_MORE` for a PE file whose resources lie beyond it (7-Zip then calls `Open()`), and `k_IsArc_Res_NO` otherwise.
- Silent: no console output and no files written by the handler. Optional debug logging only behind an explicit opt-in (for example an environment variable), off by default.

### Build

- CMake is the single build definition. Visual Studio 2022 opens the folder directly.
- Windows (MSVC, Win32 and x64): static CRT (`/MT`), `/W4 /WX`, outputs `Inno.32.dll` and `Inno.64.dll`, no imports besides Windows system DLLs (check with `dumpbin /dependents`).
- Linux (GCC): the same sources as a shared library for the test client, `-Wall -Wextra -Werror`.
- MinGW (x86_64 and i686): cross-built DLLs for running the real Windows 7-Zip under Wine (`7z-win`). Link the runtime statically (`-static`, `-static-libgcc`, `-static-libstdc++`), export undecorated names on i686 (`.def` file with `-Wl,--kill-at`), and check imports with `x86_64-w64-mingw32-objdump -p`. Only the x64 DLL can be loaded by `7z-win`; the i686 DLL is build-checked.
- C++17 unless a later standard is clearly needed.

## 5. Constraints and conventions

- Never commit installers or other binaries, third-party or generated. Test installers are built from scripts at test time.
- Source files are ASCII only, without a BOM: hyphen-minus instead of en or em dashes, straight quotes, `...` instead of an ellipsis character. Markdown may use non-ASCII only where it clearly helps.
- Keep the plugin in `InnoPlugin/` and its tooling in `.github/`; the repository root holds only `README.md`, `COPYING` and `.gitignore`.
- Keep the zlib license notices on code derived from innoextract and record the origin (innoextract path and commit) at the top of each derived file.
- Do not copy code from sources whose license is not compatible with the LGPL 2.1, such as Stack Exchange answers (CC BY-SA); write such code yourself.
- Prefer clarity over cleverness; comment only what needs explaining.
- Every phase ends with: a deliverable that works on its own, passing CI, commits with descriptive messages, and an updated "Known limitations" section in `InnoPlugin/README.md`.
- If a phase does not fit in one session, stop at a coherent point that builds and passes the tests, commit, and list the remaining work in the pull request description.
- A session ends after 59 minutes, setup included, and the full runner takes about 20 minutes under Wine for its 64-bit 7-Zip pass alone. While working, run it for a few compilers (for example `--compilers 6.2.2,7.1.0-x64`); run the full suite once before the final push, and push after each finished item so that a session that runs out of time keeps its work.
- If a requirement turns out to be wrong or impossible, choose the conservative option, keep going, and list it under "Decisions needing review" in the pull request description.

## 6. Agent environment

`.github/workflows/copilot-setup-steps.yml` (via `.github/scripts/setup-inno-tools.sh`) prepares the Ubuntu 24.04 environment:

| Tool | Purpose |
|---|---|
| `iscc-<version>` | Inno Setup command-line compiler under Wine. Versions: `5.6.1` (ANSI), `5.6.1-unicode`, `6.0.5`, `6.1.2`, `6.2.2`, `6.3.3`, `6.4.3`, `6.5.4`, `6.6.1`, `6.7.3`, `7.0.2-x64`, `7.1.0-x64`, `7.1.0-x86`. Arguments naming existing files are converted to Windows paths, so `iscc-6.7.3 /Q /Fname script.iss` works; keep paths inside the script relative to the script folder. |
| `iscc-<old version>` | The old compilers, unpacked from the installers in `/opt/inno-tools/old`: `1.3.26`, `2.0.19`, `3.0.7`, `4.0.8`, `4.0.11`, `4.1.5`, `4.1.8`, `4.2.7`, `5.1.14`, `5.2.3`, `5.3.8`, `5.3.8-unicode`, `5.5.9`, `5.5.9-unicode`. No preprocessor; call them with the script only (`iscc-4.2.7 script.iss`) and set `OutputDir` and `OutputBaseFilename` in the script, as `run_tests.py` does (`plain_script`). `iscc-1.3.26` runs `Compil32.exe /cc` with a time limit and a virtual display: it shows compile errors in a dialog and still exits with 0, so check for the output file. |
| `/opt/inno-tools/old` | The installers of all old Inno Setup versions (`InnoPlugin/tests/old-versions.txt`: 124 files, 1.2.16 to 5.6.1), checked by their SHA-256 |
| `ISCrypt.dll` | The official ARCFOUR DLL that `Encryption=yes` needs before Inno Setup 6.4, in the folders of the compilers `5.6.1` to `6.3.3` and of the old compilers, checked by its SHA-256; a stand-in built from `InnoPlugin/tests/iscrypt/iscrypt.c` if the download fails (the setup log shows which) |
| `innoextract` | innoextract 1.9 (Ubuntu package): reads installers up to 6.2.2 |
| `innoextract-master` | innoextract built from commit `6e9e34e`: reads installers up to 6.3.3; fails on 6.4.3 and later |
| `7zz` | 7-Zip 26.04 for Linux |
| `7z-win` | Windows 7-Zip 26.04 x64 under Wine; plugins go to `/opt/inno-tools/7zip-win64/Formats/` |
| `x86_64-w64-mingw32-g++`, `i686-w64-mingw32-g++` | MinGW cross compilers |
| `/opt/src/7zip`, `/opt/src/innoextract`, `/opt/src/issrc` | full clones with tags of 7-Zip, innoextract and Inno Setup |

The setup ends with a smoke test that compiles a one-file installer with every compiler; its log shows which versions the reference extractors can read. Network access during the session may be limited, so prefer the local clones.

## 7. Testing

- Corpus: `.iss` scripts and a deterministic payload generator under `InnoPlugin/tests/`. The runner compiles the scripts with the `iscc-*` compilers at test time (cache by script hash and compiler) and never commits the results. The old compilers have no preprocessor: the runner applies the preprocessor lines itself, with `NO_<feature>` defined for each feature the compiler lacks (`FEATURES` in `run_tests.py`), and builds only the variants the compiler can compile (`VARIANT_FEATURES`).
- Coverage to build up across phases: compression `none`, `zip`, `bzip`, `lzma`, `lzma2` (solid and non-solid); encryption; disk spanning; several languages; wizard images; Unicode names (Cyrillic and CJK); one source file installed to several destinations; destinations with `{code:...}` constants; an x86 executable (for the call-instruction filter); empty files; components and tasks; every installed compiler version, including ANSI 5.6.1 and both 7.1.0 builds.
- Oracle: the payload itself. Map destinations (`{app}\...`) back to payload files and compare bytes and sizes, and timestamps within the precision and time-zone rules of each version. Use `innoextract`/`innoextract-master` listings as a second opinion only where they can read the version (6.3.3 and earlier); for 6.4.3 and later the payload is the only oracle. For the old installers the oracle is what innoextract 1.9 extracts (`<name>.json` next to each installer in `InnoPlugin/build/corpus/old/`).
- Known issues: `KNOWN_ISSUES` in `run_tests.py` marks plugin problems that the tests show as expected failures (`XFAIL`). A check fails once its problem no longer shows, so fixing a known issue includes removing its entry, the rule that expects it, and its limitation in `InnoPlugin/README.md`.
- Test client: adapt upstream `CPP/7zip/UI/Client7z/Client7z.cpp` so it loads the plugin through the same exports 7-Zip uses (`CreateObject`, `GetNumberOfFormats`, `GetHandlerProperty2`, ...), with list, test and extract commands, password input and a volume callback. It builds on Linux and Windows. Like 7-Zip on Windows (`ReplaceIncorrectChars` in `CPP/7zip/UI/Common/ExtractingFilePath.cpp`), it replaces `:`, `*`, `?`, `<`, `>`, `|`, `"` and characters below 0x20 with `_` in each path component on every platform, so destinations such as `{code:GetDir}\app.exe` extract; it rejects absolute paths, `..` and empty paths.
- Runner: `python3 InnoPlugin/tests/run_tests.py` builds the corpus, runs the client and compares, with a clear pass/fail summary; the same runner works on Windows.
- Negative tests: truncated installers and flipped bytes in file data must produce per-item errors (other items still extract), never a crash or hang. Non-Inno executables (a plain PE file, and a 7z SFX made by concatenating `/opt/inno-tools/7zip-win64/7z.sfx` with a `.7z` archive, because the Linux 7-Zip package has no SFX modules) must be declined so 7-Zip falls back to its other handlers.
- End-to-end: on Linux, the MinGW DLL in `7z-win`'s `Formats` folder (`7z-win i` lists `Inno`; `l`, `t` and `x` match the payload). On Windows CI, the MSVC DLL in the official 7-Zip 26.04.
- CI: `.github/workflows/inno-plugin.yml` runs these jobs, in parallel where they can: `corpus` (the installers compiled under Wine with `run_tests.py --compile-only`, restored from the Actions cache so that only missing or outdated installers are compiled, uploaded as the artifact `inno-corpus`; a manual run with `fresh-corpus` compiles all of them), `build` (GCC, MinGW x86_64 and i686, DLL checks), `client` (the test client on the whole corpus), `wine` (a matrix: 64-bit Windows 7-Zip 26.04 under Wine in groups of compilers and of old installers (`--old-installers`), 32-bit 7-Zip with one compiler from each format group) and `windows` (a matrix: MSVC x64 with the official 64-bit 7-Zip 26.04 and MSVC Win32 with the official 32-bit 7-Zip 26.04 from https://github.com/ip7z/7zip/releases, each with the dependency check, in one job for the modern compilers and one for the old versions; the job `msvc-dlls` merges both DLLs into the artifact `inno-plugin-msvc`). Workflow runs on agent pull requests may need the owner's approval; do not rely on CI alone, run the tests in the session.

## 8. Phases

### Phase 1 - Standalone core on Linux (simplest working version; done in #1)

- Create `InnoPlugin/` with the vendored 7-Zip 26.04 subset, the CMake build (GCC), the test client, the runner, `InnoPlugin/README.md` (build and test instructions) and the `ubuntu-24.04` CI job.
- Move the current handler and innoextract port into the new layout with minimal changes: port the glue to the 26.04 interfaces, make it compile with GCC, and replace liblzma with `C/LzmaDec.c` and `C/Lzma2Dec.c`. Keep all other behavior, including the known problems.
- Corpus: `lzma` (solid), `lzma2` (non-solid) and `none`, built with `5.6.1-unicode`, `6.0.5`, `6.1.2` and `6.2.2`.
- Done when: `python3 InnoPlugin/tests/run_tests.py` passes in the session and in the Ubuntu CI job, and the old code in `CPP/` is untouched.

### Phase 2 - Windows plugin builds and end-to-end tests (done in #1)

- Build `Inno.32.dll` and `Inno.64.dll` from the same CMake project with MSVC (static CRT, `/W4 /WX`) and MinGW (x86_64 and i686), with the export set from section 4.
- End-to-end tests through real 7-Zip: the x64 MinGW DLL in `7z-win`'s `Formats` folder in the session and the Ubuntu job, and the MSVC DLLs in the official 7-Zip 26.04 (x64 and x86 installers) in the new `windows-2022` job.
- Extend `InnoPlugin/README.md` with install steps.
- Done when: `7z i` lists `Inno` from the plugin, the corpus passes through real 7-Zip on Linux (Wine) and Windows, and the DLLs import only Windows system DLLs.

### Phase 3 - Native data pipeline (done in #2)

- Replace the stream-chain shims with the architecture in section 4: memory-backed header parsing, 7-Zip streams and decoders for all compression methods, executable filter and checksum verification in the 7-Zip pipeline; remove `minihead_iostreams.hpp`.
- Corpus: add `zip` and `bzip`, solid and non-solid, and ANSI `5.6.1`.
- Done when: every method extracts byte-identical payloads, checksum mismatches are detected, and no Boost shim remains in the data path.

### Phase 4 - Correct 7-Zip integration (done in #3)

- First, cover the call-instruction filter in CI. Inno Setup applies it only to compressed x86/x64 executables (`IsX86OrX64Executable` in its `Projects/Compile.pas`), and the payload has none, so no filter code runs in the tests. Have `make_payload.py` generate a deterministic synthetic executable of a few hundred KiB (`MZ` header whose `e_lfanew` points to `PE\0\0` with machine `0x014C`; the rest of the file need not be a valid program) whose body contains many `E8`/`E9` bytes followed by addresses with a high byte of `0x00` or `0xFF`, including instructions in the last 4 bytes of a 64 KiB block and at the very end of the file, and install it in every variant. The corpus compilers then exercise the 5.3.9+ variant (`InstructionFilter5309`). Check through the item's method property (`kpidMethod`, section 4), which names the filter (for example `IF5309`), that the filter actually ran; otherwise the test can pass without exercising it. The older variants are covered by the owner's real installers until Phase 7.
- Fix section 3 items 3 to 9: progress, test mode, items without an output stream, per-item results, data-order extraction, shared data without unbounded buffers, UTF-8 names and comments, properties including `kpidPhySize`, `IsArc_Inno`, removal of console output and log files, and cancellation.
- Tests: `t`, extraction of a subset in reverse order, "skip existing files", Unicode names, duplicates, the negative tests, progress totals (total equals the sum of extracted sizes, completed values never decrease), and a non-Inno `.exe` still opening as `PE` or `7z`.

### Phase 5 - Newer Inno Setup versions and encryption (done in #4)

- Port the innoextract changes from `83c1544b3f` (the base of the current code) to `6e9e34e` (6.3.0, 6.4.0, 6.4.0.1, XChaCha20, PBKDF2-SHA256), then add 6.4.1 and later up to 7.1.x from the Inno Setup sources.
- Passwords through `ICryptoGetTextPassword`: ARC4 (MD5 or SHA-1 keyed) for older versions, the newer scheme for 6.4 and later; check the password hash, report `kWrongPassword`.
- Corpus: every installed compiler; encrypted variants (`Password=` with `Encryption=yes`, plus any newer encryption modes) for each scheme.

### Phase 6 - Disk spanning, cleanup, retire the old integration (done in #5)

- Multi-part installers through `IArchiveOpenVolumeCallback`; corpus with `DiskSpanning=yes` and a small `DiskSliceSize`, including a missing slice.
- Limit the decompressed size of the setup header blocks. `DecodeLzma1Buffer` and `DecodeZlibBuffer` in `InnoPlugin/src/InnoDecoder.cpp` grow their output without bound, so a crafted `.exe` makes 7-Zip use a lot of memory while it merely opens the file. Fail the block above a fixed limit far beyond real installers, and add a negative test.
- Name the embedded wizard images by their content: Inno Setup 6.5.2 and later store PNG images, which are listed as `.bmp`. List the back and dark wizard image sets too.
- Remove dead code (`cli/`, unused helpers, `magic_enum` if unneeded).
- Remove the old integration: `CPP/7zip/Archive/Inno/`, `CPP/7zip/Bundles/InnoPlugin/`, the Inno entries in `CPP/7zip/Bundles/Format7zF/`, and revert the `IArchive.h` and `resource.rc` hacks. Update the root `README.md` to point to `InnoPlugin/README.md`.
- Final `InnoPlugin/README.md`: install steps, supported Inno Setup versions table, `-tInno` for renamed files, known limitations.

### Phase 7 - Inno Setup versions before 5.6 in CI (done in #6)

The corpus compilers start at 5.6.1, so some format paths are covered only by the owner's real installers: the call-instruction filters before 5.3.9 (`InstructionFilter4108` before 5.2.0, `InstructionFilter5200` from 5.2.0 to 5.3.8), zlib-compressed setup headers (4.0.9 to 4.1.5) and the older header layout (before 4.0.9).

- Old versions are at `https://files.jrsoftware.org/is/<major>/` (folders `1.3`, `2`, `3`, `4` and `5`, linked from https://jrsoftware.org/isdl-old.php), for example `isetup-2.0.19.exe`, `isetup-3.0.7.exe`, `isetup-4.0.8.exe`, `isetup-4.1.5.exe`, `isetup-4.2.7.exe`, `isetup-5.1.14.exe`, `isetup-5.2.3.exe`, `isetup-5.3.8.exe` and `isetup-5.3.8-unicode.exe`. Download them at test time (extend `setup-inno-tools.sh` or the runner) and cache them; never commit them.
- These files are themselves Inno Setup installers, each built with a version close to its own (`isetup-5.1.14.exe` is a 5.1.13 installer), so they are ready-made test data: extracting them must succeed with every stored checksum verified, and match `innoextract`.
- Build own test installers with the old compilers. Unpack the compilers with `innoextract` instead of running their installers; every version from 2.0.19 on includes `ISCC.exe` (1.3.26 only `Compil32.exe`, used as `Compil32.exe /cc`). None includes the preprocessor, so the runner writes a plain `.iss` per version that uses only the directives that version supports. Include the synthetic executable from Phase 4 and check through the item method that each filter variant ran.
- On Windows (not under Wine), UAC treats the old `ISCC.exe` as an installer and asks for elevation; set `__COMPAT_LAYER=RunAsInvoker` for that process.
- CI: add the old compilers to the `corpus` job and as new entries of the `wine` matrix (groups of compilers), so that no job runs much longer than the others; the `client` and `windows` jobs test the whole corpus.
- Already verified by the owner: the Phase 6 plugin lists, extracts and tests (`7z t`, every checksum) `isetup-1.3.26.exe`, `2.0.19`, `3.0.7`, `4.0.8`, `4.0.9`, `4.1.5`, `4.1.6`, `4.2.7`, `5.1.14` and `5.3.8` without errors, and installers built natively with `ISCC.exe` 2.0.19 (bzip) and 5.1.14 (LZMA, solid) containing a synthetic executable extract byte-identically.
- Done when CI covers each format boundary above, with the payload and `innoextract` as oracles.

### Phase 8 - Known issues of the old versions, newer setup data versions (done in #7)

Phase 7 found three plugin problems (items 1 to 3). The tests report them as expected failures (`XFAIL`, `KNOWN_ISSUES` in `InnoPlugin/tests/run_tests.py`), and `InnoPlugin/README.md` lists them under Known limitations. Fix all three, and read installers with a setup data version newer than the plugin knows (item 4). A check that expects a known issue fails once the problem no longer shows, so each fix also removes its `KNOWN_ISSUES` entry, the rule that expects it (`old_issues` or `installer_issues`) and its README limitation. Reproduce them in the session: `/opt/inno-tools/old` has the old installers and `iscc-<old version>` the old compilers (section 6); the traces below come from `inno_client` with the plugin of Phase 7 (`InnoPlugin/build/linux/Inno.so` in the session).

1. `uninstaller`: before Inno Setup 5.0, Setup's uninstaller is stored as a file entry without a destination: `setup::file_entry::type` is `UninstExe` (before 5.0 there is also `RegSvrExe`, the helper for the `regserver` flag). The plugin lists it with an empty path, and the client refuses to extract (exit code 3):

   ```
   $ inno_client Inno.so l /opt/inno-tools/old/isetup-4.2.7.exe
   {"type":"item","index":0,"path":"","size":67072,"mtime":127327680000000000,"solid":true,"encrypted":false,"method":"LZMA IF4108"}
   $ inno_client Inno.so x /opt/inno-tools/old/isetup-4.2.7.exe out
   {"type":"error","message":"Unsafe path in archive"}
   {"type":"extract_result","mode":"extract","hresult":"0x80004004","items":0,"errors":0,"skipped":0,"total":3309538,"completed":0,"completed_decreased":false}
   ```

   7-Zip lists it as `Path = isetup-4.2.7` and extracts it to a file of that name; innoextract skips such entries (`cli/extract.cpp`: "Internal file, not extracted"). All 40 old installers before 5.0 have one (59392 bytes, "Inno Setup Uninstaller", in 1.3.26). Fix: list `UninstExe` entries as `embedded\uninstall.exe` and `RegSvrExe` entries as `embedded\regsvr.exe` (and add both to the table of `embedded\` items in `InnoPlugin/README.md`), so that `t` still verifies their data; no item may have an empty path, so list any other entry without a destination as `embedded\<index>.bin`. In the tests, remove `Uninstallable=no` from `plain_script`, expect `embedded\uninstall.exe` in the corpus installers of the old compilers before 5.0, and drop the `-i` subset in `test_client_old` and the `skip` in `test_sevenzip_old`.
2. `codepage`: ANSI installers (Inno Setup up to 5.6.1 without `-unicode`) store text in the code page of a language. `InnoPlugin/src/inno/minihead.hpp` defines neither `INNOEXTRACT_HAVE_WIN32_CONV` nor `INNOEXTRACT_HAVE_ICONV`, so `util::to_utf8` (`util/encoding.cpp`) uses `to_utf8_fallback` for every code page except 1252, ISO-8859-1 and UTF-16LE: characters outside ASCII become `_`, and it logs `Unknown data while converting from CP<n> to UTF-8.`, which becomes the archive warning:

   ```
   $ inno_client Inno.so l /opt/inno-tools/old/isetup-4.2.7.exe
   {"type":"archive","solid":true,"method":"LZMA IF4108","comment":"InnoSetupVersion = 4.2.6\u000aAppName = Inno Setup\u000aAppVersion = 4.2.7\u000a","phy_size":1011323,"headers_size":41410,"warning":"Unknown data while converting from CP1250 to UTF-8.\u000aUnknown data while converting from CP1251 to UTF-8."}
   ```

   All 63 ANSI old installers from 4.2.1 on show it (their translations use the code pages 1250, 1251, 1253, 1254, 1255 and 932), and 7-Zip prints "Archives with Warnings: 1". The messages and license texts of those languages lose their non-ASCII characters (`embedded\<language>.isl`). File names are affected when no language of the installer uses code page 1252, because `setup/info.cpp` then decodes the headers with the code page of the first language: a 5.6.1 ANSI installer with only `[Languages] Name: "ru"; MessagesFile: "compiler:Default.isl"` and `[LangOptions] LanguageCodePage=1251`, with a file whose name has the byte 0xE9 (e acute in CP1252, short i in CP1251), lists `{app}\caf_.txt` with the warning, while innoextract lists the name with the CP1251 character. Fix: convert with `MultiByteToWideChar` on Windows (`INNOEXTRACT_HAVE_WIN32_CONV`; the code is in `util/encoding.cpp`) and with iconv on Linux (`INNOEXTRACT_HAVE_ICONV`, glibc), keeping the fallback with its warning for code pages the system cannot convert. Add corpus coverage: an ANSI variant (5.6.1 and an old compiler) whose only language uses another code page (for example `compiler:Languages\Russian.isl`, CP1251), with non-ASCII file names in that code page, and check the item paths and that there is no warning. The ANSI compiler reads file names in Wine's ANSI code page (1252 by default): either compile with a Russian locale for Wine (for example `LANG=ru_RU.UTF-8`, after `locale-gen` if Wine needs it) and Cyrillic payload names, or use payload names in the CP1252 range and expect their CP1251 meaning.
3. `slices-1.3`: Inno Setup 1.3 names the slices `<name>.1`, `<name>.2`, ... (1.3.26 `Projects/Compile.pas`: `OutputBaseFilename + '.' + IntToStr(CurDisk)`; 2.0.19 and later write `'%s-%d.bin'`), with the same slice header (`idska32\x1a`). `GetSliceName` in `InnoPlugin/src/InnoIn.cpp`, like innoextract's `stream/slice.cpp`, builds only `<name>-<n>.bin`, so the files in the slices are "Unavailable":

   ```
   $ inno_client Inno.so l InnoPlugin/build/corpus/installers/1.3.26/spanned.exe
   {"type":"archive","solid":false,"method":"Deflate","comment":"InnoSetupVersion = 1.3.25\u000aAppName = InnoPluginTest\u000aAppVersion = 1.0\u000a","phy_size":233386,"headers_size":2198,"warning":"Missing slice: spanned-1.bin\u000aMissing slice: spanned-2.bin\u000aMissing slice: spanned-3.bin"}
   ```

   Fix: for setup data before 2.0.0 look for `<name>.<n>`, with `<name>` as now (the base file name from the headers before 4.1.7, otherwise the name of the installer). The missing-slice test then covers 1.3.26 too.
4. Newer setup data versions: an installer whose setup data version is newer than the newest the plugin knows (`7.0.0.3`, Inno Setup 7.1.0) opens as an empty archive with the error "Unsupported Inno Setup version: <version>" (`kpv_ErrorFlags_UnsupportedFeature`): `CInArchive::Open` (`InnoPlugin/src/InnoIn.cpp`) stops when `Info.version.is_newer_than_known()`, and `info::load_version` (`src/inno/setup/info.cpp`) skips the 6.5+ encryption header for such versions. Read them with the format of the newest known version instead, as innoextract does unless `--no-extract-unknown`: take the version number from the version string as now, parse with the newest known setup data version (read the encryption header, load the headers), and report one archive warning (`kpidWarning`) that names both versions, for example `Unknown Inno Setup version 9.9.9.9, read as 7.0.0.3`; `kpidComment` keeps the version of the installer. If the installer cannot be read that way (for example the encryption header CRC, a header block or an entry fails), keep the current result: an empty archive with the error "Unsupported Inno Setup version: <version>", never a crash or hang. Unknown versions between known ones keep their current handling. In the tests, replace the single check of `make_unsupported` (the newest installer of the run with its data version changed to `9.9.9.9`, checked in `test_client_encryption` and `test_sevenzip_encryption`) with two copies, each checked only when its installer is in the run (the `--compilers` subsets of the CI jobs differ):
   - a `7.1.0-x64` or `7.1.0-x86` installer with `(7.0.0.3)` changed to `(9.9.9.9)`: it lists the same items as the original, the archive warning names both versions, there is no error, client `t` and `7z t` pass, and 7-Zip shows the warning;
   - a `5.6.1` installer with `(5.5.7)` changed to `(9.9.9)`: the newest format does not fit (the encryption header CRC fails), so it opens with the error and no items, and `7z l` exits with code 2 and prints "Unsupported Inno Setup version".

   Update the README paragraph on newer versions and the "newer setup data versions" row of its supported-versions table, and the description of these checks in `InnoPlugin/tests/README.md`.

- Done when `KNOWN_ISSUES` is empty, the runner reports no `XFAIL` in any CI job, `InnoPlugin/README.md` no longer lists these limitations, installers with a newer setup data version open with the newest known format and a warning, and the pull request checklist (section 9) is met.

## 9. Pull request checklist

- Summary of changes per area.
- Verification: exact commands run in the session and their results (test runner summary, `7z-win i` output, CI links).
- Known limitations after this phase (also in `InnoPlugin/README.md`).
- Decisions needing review.
- Confirmation that no binaries were committed and new or changed source files are ASCII only.

## 10. References

- 7-Zip: https://github.com/ip7z/7zip (NSIS handler `CPP/7zip/Archive/Nsis/`, plugin exports `CPP/7zip/Archive/DllExports.cpp`, client sample `CPP/7zip/UI/Client7z/`, plugin loading and format order in `CPP/7zip/UI/Common/LoadCodecs.cpp` and `OpenArchive.cpp`).
- innoextract: https://github.com/dscharrer/innoextract (format notes in its sources and README).
- Inno Setup sources: https://github.com/jrsoftware/issrc; documentation: https://jrsoftware.org/ishelp/ (`[Setup]` directives `Compression`, `SolidCompression`, `Encryption`, `Password`, `DiskSpanning`, `DiskSliceSize`).
