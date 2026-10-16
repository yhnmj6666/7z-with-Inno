# Git archive of 7-zip releases

Because 7-zip [doesn't](https://github.com/7z) have a public repository, and their code is released only as archives on SourceForge, I maintain a git-ified copy here.

The [tagged releases](https://github.com/pornel/7z/releases) and the [original branch](https://github.com/pornel/7z/tree/original) contain pure 7-zip releases, without any modifications like this readme.

## Inno Setup plugin

`CPP/7zip/Bundles/InnoPlugin` builds the Inno Setup handler (`CPP/7zip/Archive/Inno`) as a standalone 7-Zip format plugin, so stock 7-Zip builds can open Inno Setup installers without a custom `7z.dll`. The plugin uses the stable 7-Zip plugin interface (`CreateObject`, `GetHandlerProperty2`, `IInArchive`, ...), so updating 7-Zip does not require rebuilding it. Tested with 7-Zip 25.01 (x64).

### Build

Open `CPP\7zip\Bundles\InnoPlugin\InnoPlugin.sln` in Visual Studio 2022, or from a Developer Command Prompt:

```
msbuild CPP\7zip\Bundles\InnoPlugin\InnoPlugin.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `bin\InnoPlugin\x64\Release\Inno.64.dll` (`Platform=Win32` builds `Inno.32.dll`).

### Install

1. Create a `Formats` folder next to `7z.dll`, e.g. `C:\Program Files\7-Zip\Formats`.
2. Copy `Inno.64.dll` (64-bit 7-Zip) or `Inno.32.dll` (32-bit 7-Zip) into it. Copy only the one that matches your 7-Zip.
3. `7z i` lists the DLL under `Libs:` and `Inno` under `Formats:`.

7z.exe, 7zG.exe and 7zFM.exe load every DLL in `Formats` at startup, and 7-Zip upgrades leave that folder in place.

The plugin needs the Microsoft Visual C++ 2015-2022 Redistributable, because the bundled liblzma libraries (`CPP/7zip/Archive/Inno/InnoExtract/liblzma/lib`) are built against the DLL CRT.

### How installers are detected

7-Zip sorts formats by name and tries every format registered for the file's extension in that order, so `Inno` gets the first try on any `.exe`, before `PE`. Signed installers therefore open as Inno archives too. Executables that are not Inno Setup installers are declined, and 7-Zip falls back to its usual PE / NSIS / SFX detection. For other file names (for example a renamed `setup.bin`), force the type: `7z x -tInno setup.bin`.

### Known limitations

- Installers split into external `*.bin` files (disk spanning) are not supported; extraction reports "Not implemented".
- Test mode (`7z t`, the GUI Test button) reports "Unspecified error", because the handler expects an output stream for every item.
