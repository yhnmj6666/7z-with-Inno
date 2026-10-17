; Shared part of the test corpus scripts (see ../README.md). The variant
; scripts define Compression and Solid and include this file. Keep paths
; relative: the runner compiles the scripts next to the generated payload.
; run_tests.py reads the [Files] section to map destinations back to payload
; files, so keep one entry per line with Source and DestDir (and optionally
; DestName), and no wildcards.

[Setup]
AppName=InnoPluginTest
AppVersion=1.0
DefaultDirName={sd}\InnoPluginTest
DisableProgramGroupPage=yes
Compression={#Compression}
SolidCompression={#Solid}

[Files]
Source: "payload\readme.txt"; DestDir: "{app}"
Source: "payload\empty.dat"; DestDir: "{app}"
Source: "payload\docs\manual.txt"; DestDir: "{app}\docs"
Source: "payload\data\random.bin"; DestDir: "{app}\data"
Source: "payload\data\nested\deeper\leaf.txt"; DestDir: "{app}\data\nested\deeper"
; One source file installed to two destinations: Inno Setup stores the data once.
Source: "payload\shared.txt"; DestDir: "{app}"
Source: "payload\shared.txt"; DestDir: "{app}\copy"
; A destination computed by a [Code] function.
Source: "payload\code.txt"; DestDir: "{code:GetDestDir}"

[Code]
function GetDestDir(Param: String): String;
begin
  Result := ExpandConstant('{app}\code');
end;
