# Git archive of 7-zip releases

Because 7-zip [doesn't](https://github.com/7z) have a public repository, and their code is released only as archives on SourceForge, I maintain a git-ified copy here.

The [tagged releases](https://github.com/pornel/7z/releases) and the [original branch](https://github.com/pornel/7z/tree/original) contain pure 7-zip releases, without any modifications like this readme.

## Inno Setup plugin

[`InnoPlugin/`](InnoPlugin/README.md) holds a format plugin for stock 7-Zip that opens, lists, tests and extracts Inno Setup installers: copy `Inno.64.dll` or `Inno.32.dll` into the `Formats` folder of 7-Zip. Building, installing, supported Inno Setup versions and known limitations are described in [`InnoPlugin/README.md`](InnoPlugin/README.md).
