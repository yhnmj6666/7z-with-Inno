# Rewrite the Inno Setup plugin for 7-Zip

Prompt for the GitHub Copilot cloud agent. Start each task with:

> Implement Phase N of `.github/prompts/inno-plugin-refactor.prompt.md`.

Work on exactly one phase per task and pull request, based on the `main` branch (the branch `original` is an unrelated mirror of plain 7-Zip releases). Read the whole file before starting; sections 1 to 7 apply to every phase.

Owner prerequisites: the repository default branch must contain `.github/workflows/copilot-setup-steps.yml`, otherwise the agent environment in section 6 is not installed; select `main` as the base branch when starting a task; approve workflow runs on agent pull requests when asked.

## 1. Goal

Open, list, test and extract Inno Setup installers in stock 7-Zip (7z.exe, 7zG.exe, 7zFM.exe) through a format plugin DLL that users copy into `<7-Zip folder>\Formats\`. 7-Zip updates must never require rebuilding the plugin. The current code works but is a quick port with known defects; rewrite it into a maintainable, well-tested plugin.

## 2. Current state

- Phases 1 and 2 are done (#1): `InnoPlugin/` holds the standalone plugin (vendored 7-Zip 26.04, CMake build, test client, corpus, CI), with the handler in `InnoPlugin/src/` and the innoextract port in `InnoPlugin/src/inno/`. Phases 4 to 6 change this code; the problems in section 3 still apply to it, except problems 1 and 2, which Phase 3 fixed. The original code below stays under `CPP/` as the parity reference until Phase 6.
- Phase 3 is done (#2): headers are decompressed into memory, and file data goes through 7-Zip streams and decoders (`InnoPlugin/src/InnoDecoder.*`, Deflate/zlib and BZip2 vendored from 7-Zip 26.04). `minihead_iostreams.hpp` is gone; checksum mismatches are reported as CRC errors, and encrypted files as unsupported until Phase 5. The rewritten call-instruction filter matches innoextract and passes the owner's real installers, but has no CI coverage yet (first item of Phase 4).
- The original code: this repository is a git copy of the 7-Zip 22.00 source tree with an Inno Setup handler added:
  - `CPP/7zip/Archive/Inno/` - 7-Zip glue: `InnoHandler.cpp` (IInArchive), `InnoIn.*` (CInArchive, CEmbeddedFiles), `InnoInterop.*` (std::streambuf over IInStream, progress writer), `InnoItem.*`, `InnoRegister.cpp` (`REGISTER_ARC_I_NO_SIG("Inno", "exe", 0, 0x70, ...)`, flags `kPureStartOpen | kUseGlobalOffset`).
  - `CPP/7zip/Archive/Inno/InnoExtract/` - partial port of innoextract (https://github.com/dscharrer/innoextract, zlib license), copied in 2022 from master at commit `83c1544b3f` (2021-12-02, newer than release 1.9); the owner later applied the equivalent of upstream fix `325a7c9655` to `crypto/iteratedhash.hpp`. Boost is replaced by shims written for this repository (`minihead.hpp`, `minihead_iostreams.hpp`); LZMA comes from prebuilt MSVC liblzma libraries (`liblzma/lib/*.lib`, DLL CRT). `cli/` is not compiled, and `lib/magic_enum.hpp` is included by `InnoIn.cpp` but unused.
  - `CPP/7zip/Bundles/InnoPlugin/InnoPlugin.vcxproj` - builds the plugin (`Inno.64.dll`, `Inno.32.dll`), MSVC 2022 only, against this repository's 7-Zip 22.00 sources. See the "Inno Setup plugin" section of `README.md`.
- The owner verifies releases locally with 15 real installers (Inno Setup 5.0.4 to 6.2, signed and unsigned): the original plugin with 7-Zip 25.01, and the Phase 1-2 build with the official 7-Zip 26.04 x64 and x86, which lists and extracts them byte-identically to the original. Those installers are third-party software and must never be added to this public repository.
- How 7-Zip picks the handler: 7-Zip sorts formats by name (case-sensitive) and offers a file to every format registered for its extension, in that order. `Inno` is registered for `exe` and sorts before `PE`, so it gets the first try on every `.exe`, including signed installers that `PE` would otherwise claim. Consequences: keep the format name `Inno`, and make `Open()` reject non-Inno executables quickly and silently with `S_FALSE`.
- The older "built into 7z.dll" integration is still present (`CPP/7zip/Bundles/Format7zF/Format7z.vcxproj`, plus two hacks in the 7-Zip tree: `CPP/7zip/Archive/IArchive.h` flips `#ifdef _MSC_VER` to `#ifndef` around `MY_NO_THROW_DECL_ONLY`, and `CPP/7zip/UI/FileManager/resource.rc` comments out the manifest). It is retired in Phase 6.

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

- Listing: `kpidPath` (UTF-16 converted from UTF-8; keep Inno constants such as `{app}` as the first path component), `kpidSize`, `kpidMTime`, `kpidAttrib`, `kpidEncrypted`, `kpidMethod`, `kpidCRC` (only when the entry checksum is CRC-32), `kpidSolid`. Archive properties: `kpidMethod`, `kpidSolid`, `kpidPhySize`, `kpidHeadersSize`, `kpidComment` (Inno Setup version, app name and version), `kpidError`/`kpidWarning` only for real problems.
- Entries with identical destination paths get deterministic, distinct names; document the scheme (compare innoextract's `--collisions=rename`).
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
- Until Phase 6, do not change files outside `InnoPlugin/` and `.github/` (the old code in `CPP/` is the parity reference).
- Keep the zlib license notices on code derived from innoextract and record the origin (innoextract path and commit) at the top of each derived file.
- Do not copy code from sources whose license is not compatible with the LGPL 2.1, such as Stack Exchange answers (CC BY-SA); write such code yourself.
- Prefer clarity over cleverness; comment only what needs explaining.
- Every phase ends with: a deliverable that works on its own, passing CI, commits with descriptive messages, and an updated "Known limitations" section in `InnoPlugin/README.md`.
- If a phase does not fit in one session, stop at a coherent point that builds and passes the tests, commit, and list the remaining work in the pull request description.
- If a requirement turns out to be wrong or impossible, choose the conservative option, keep going, and list it under "Decisions needing review" in the pull request description.

## 6. Agent environment

`.github/workflows/copilot-setup-steps.yml` (via `.github/scripts/setup-inno-tools.sh`) prepares the Ubuntu 24.04 environment:

| Tool | Purpose |
|---|---|
| `iscc-<version>` | Inno Setup command-line compiler under Wine. Versions: `5.6.1` (ANSI), `5.6.1-unicode`, `6.0.5`, `6.1.2`, `6.2.2`, `6.3.3`, `6.4.3`, `6.5.4`, `6.6.1`, `6.7.3`, `7.0.2-x64`, `7.1.0-x64`, `7.1.0-x86`. Arguments naming existing files are converted to Windows paths, so `iscc-6.7.3 /Q /Fname script.iss` works; keep paths inside the script relative to the script folder. |
| `innoextract` | innoextract 1.9 (Ubuntu package): reads installers up to 6.2.2 |
| `innoextract-master` | innoextract built from commit `6e9e34e`: reads installers up to 6.3.3; fails on 6.4.3 and later |
| `7zz` | 7-Zip 26.04 for Linux |
| `7z-win` | Windows 7-Zip 26.04 x64 under Wine; plugins go to `/opt/inno-tools/7zip-win64/Formats/` |
| `x86_64-w64-mingw32-g++`, `i686-w64-mingw32-g++` | MinGW cross compilers |
| `/opt/src/7zip`, `/opt/src/innoextract`, `/opt/src/issrc` | full clones with tags of 7-Zip, innoextract and Inno Setup |

The setup ends with a smoke test that compiles a one-file installer with every compiler; its log shows which versions the reference extractors can read. Network access during the session may be limited, so prefer the local clones.

## 7. Testing

- Corpus: `.iss` scripts and a deterministic payload generator under `InnoPlugin/tests/`. The runner compiles the scripts with the `iscc-*` compilers at test time (cache by script hash and compiler) and never commits the results. Older compilers reject newer directives and constants (for example `{autopf}`): use preprocessor conditionals (`#if Ver >= ...`) where the compiler supports them, otherwise per-version script variants.
- Coverage to build up across phases: compression `none`, `zip`, `bzip`, `lzma`, `lzma2` (solid and non-solid); encryption; disk spanning; several languages; wizard images; Unicode names (Cyrillic and CJK); one source file installed to several destinations; destinations with `{code:...}` constants; an x86 executable (for the call-instruction filter); empty files; components and tasks; every installed compiler version, including ANSI 5.6.1 and both 7.1.0 builds.
- Oracle: the payload itself. Map destinations (`{app}\...`) back to payload files and compare bytes and sizes, and timestamps within the precision and time-zone rules of each version. Use `innoextract`/`innoextract-master` listings as a second opinion only where they can read the version (6.3.3 and earlier); for 6.4.3 and later the payload is the only oracle.
- Test client: adapt upstream `CPP/7zip/UI/Client7z/Client7z.cpp` so it loads the plugin through the same exports 7-Zip uses (`CreateObject`, `GetNumberOfFormats`, `GetHandlerProperty2`, ...), with list, test and extract commands, password input and a volume callback. It builds on Linux and Windows. Like 7-Zip on Windows (`ReplaceIncorrectChars` in `CPP/7zip/UI/Common/ExtractingFilePath.cpp`), it replaces `:`, `*`, `?`, `<`, `>`, `|`, `"` and characters below 0x20 with `_` in each path component on every platform, so destinations such as `{code:GetDir}\app.exe` extract; it rejects absolute paths and `..`.
- Runner: `python3 InnoPlugin/tests/run_tests.py` builds the corpus, runs the client and compares, with a clear pass/fail summary; the same runner works on Windows.
- Negative tests: truncated installers and flipped bytes in file data must produce per-item errors (other items still extract), never a crash or hang. Non-Inno executables (a plain PE file, and a 7z SFX made by concatenating `/opt/inno-tools/7zip-win64/7z.sfx` with a `.7z` archive, because the Linux 7-Zip package has no SFX modules) must be declined so 7-Zip falls back to its other handlers.
- End-to-end: on Linux, the MinGW DLL in `7z-win`'s `Formats` folder (`7z-win i` lists `Inno`; `l`, `t` and `x` match the payload). On Windows CI, the MSVC DLL in the official 7-Zip 26.04.
- CI: `.github/workflows/inno-plugin.yml` with an `ubuntu-24.04` job (GCC build and tests from Phase 1; MinGW and Wine end-to-end from Phase 2; reuse `setup-inno-tools.sh`) and, from Phase 2, a `windows-2022` job (MSVC Win32 and x64, dependency check, the corpus compiled by the Ubuntu job passed in as an artifact instead of installing Inno Setup, official 7-Zip 26.04 x64 and x86 from https://github.com/ip7z/7zip/releases extracted into folders, end-to-end tests, DLLs uploaded as artifacts). Workflow runs on agent pull requests may need the owner's approval; do not rely on CI alone, run the tests in the session.

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

### Phase 4 - Correct 7-Zip integration

- First, cover the call-instruction filter in CI. Inno Setup applies it only to compressed x86/x64 executables (`IsX86OrX64Executable` in its `Projects/Compile.pas`), and the payload has none, so no filter code runs in the tests. Have `make_payload.py` generate a deterministic synthetic executable of a few hundred KiB (`MZ` header whose `e_lfanew` points to `PE\0\0` with machine `0x014C`; the rest of the file need not be a valid program) whose body contains many `E8`/`E9` bytes followed by addresses with a high byte of `0x00` or `0xFF`, including instructions in the last 4 bytes of a 64 KiB block and at the very end of the file, and install it in every variant. The corpus compilers then exercise the 5.3.9+ variant (`InstructionFilter5309`); the older variants are covered only by the owner's real installers.
- Fix section 3 items 3 to 9: progress, test mode, items without an output stream, per-item results, data-order extraction, shared data without unbounded buffers, UTF-8 names and comments, properties including `kpidPhySize`, `IsArc_Inno`, removal of console output and log files, and cancellation.
- Tests: `t`, extraction of a subset in reverse order, "skip existing files", Unicode names, duplicates, the negative tests, progress totals (total equals the sum of extracted sizes, completed values never decrease), and a non-Inno `.exe` still opening as `PE` or `7z`.

### Phase 5 - Newer Inno Setup versions and encryption

- Port the innoextract changes from `83c1544b3f` (the base of the current code) to `6e9e34e` (6.3.0, 6.4.0, 6.4.0.1, XChaCha20, PBKDF2-SHA256), then add 6.4.1 and later up to 7.1.x from the Inno Setup sources.
- Passwords through `ICryptoGetTextPassword`: ARC4 (MD5 or SHA-1 keyed) for older versions, the newer scheme for 6.4 and later; check the password hash, report `kWrongPassword`.
- Corpus: every installed compiler; encrypted variants (`Password=` with `Encryption=yes`, plus any newer encryption modes) for each scheme.

### Phase 6 - Disk spanning, cleanup, retire the old integration

- Multi-part installers through `IArchiveOpenVolumeCallback`; corpus with `DiskSpanning=yes` and a small `DiskSliceSize`, including a missing slice.
- Remove dead code (`cli/`, unused helpers, `magic_enum` if unneeded).
- Remove the old integration: `CPP/7zip/Archive/Inno/`, `CPP/7zip/Bundles/InnoPlugin/`, the Inno entries in `CPP/7zip/Bundles/Format7zF/`, and revert the `IArchive.h` and `resource.rc` hacks. Update the root `README.md` to point to `InnoPlugin/README.md`.
- Final `InnoPlugin/README.md`: install steps, supported Inno Setup versions table, `-tInno` for renamed files, known limitations.

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
