; Shared part of the timestamp variants (see ../README.md). They define
; TimeStampsInUTC and include this file. run_tests.py compiles them with
; TZ=UTC; TimeStampRounding=0 keeps the odd seconds of the payload times.

[Setup]
AppName=InnoPluginTimestamps
AppVersion=1.0
DefaultDirName={sd}\InnoPluginTimestamps
DisableProgramGroupPage=yes
Compression=lzma2
SolidCompression=yes
TimeStampsInUTC={#TimeStampsInUTC}
TimeStampRounding=0

[Files]
Source: "payload\time\winter.txt"; DestDir: "{app}"
Source: "payload\time\summer.txt"; DestDir: "{app}"
