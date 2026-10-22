# Inno Setup plugin for 7-Zip

A format plugin that lets stock 7-Zip (7z.exe, 7zG.exe, 7zFM.exe) open, list,
test and extract Inno Setup installers, including encrypted and disk-spanning
ones: copy `Inno.64.dll` (64-bit 7-Zip) or `Inno.32.dll` (32-bit 7-Zip) into
the `Formats` folder of 7-Zip. Installation, building, testing, the supported
Inno Setup versions and the known limitations are described in
[`InnoPlugin/README.md`](InnoPlugin/README.md).

## Repository layout

| Path | Contents |
|---|---|
| [`InnoPlugin/`](InnoPlugin/) | the plugin: sources, CMake build, vendored 7-Zip 26.05 subset, tests |
| [`.github/workflows/inno-plugin.yml`](.github/workflows/inno-plugin.yml) | CI: builds with GCC, MinGW and MSVC, tests under Wine and on Windows |
| [`.github/`](.github/) | the environment and the brief of the GitHub Copilot cloud agent |

## History

This repository started as a fork of
[kornelski/7z](https://github.com/kornelski/7z), a git archive of 7-Zip
releases, and the first version of the plugin was built inside its 7-Zip 22.00
source tree. The plugin now builds on its own, so the 7-Zip sources were
removed; the release tags (`15.05` to `22.00`) and the earlier commits keep
them.

## License

GNU LGPL 2.1 or later (see [`COPYING`](COPYING)), with parts under the zlib
license (code derived from innoextract) and the Boost Software License; see
the License section of [`InnoPlugin/README.md`](InnoPlugin/README.md).
