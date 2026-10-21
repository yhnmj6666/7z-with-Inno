; Shared part of the test corpus scripts (see ../README.md). The variant
; scripts define Compression and Solid and include this file. Keep paths
; relative: the runner compiles the scripts next to the generated payload.
; run_tests.py reads the [Files] section to map destinations back to payload
; files, so keep one entry per line with Source and DestDir (and optionally
; DestName). A Source ending in "\*" takes the payload files in that folder
; (with Flags: recursesubdirs, also in its subfolders). A line
; "; expect: <item path>" gives the item path of the next entry, for entries
; listed with leading folders.
; The old compilers have no preprocessor: run_tests.py applies the
; preprocessor lines itself, with SMALL_CORPUS defined and NO_<feature> for
; each feature that the compiler lacks (FEATURES in run_tests.py).

[Setup]
AppName=InnoPluginTest
AppVersion=1.0
DefaultDirName={sd}\InnoPluginTest
DisableProgramGroupPage=yes
#ifndef NO_COMPRESSION
Compression={#Compression}
#endif
#ifndef NO_SOLID
SolidCompression={#Solid}
#endif
#ifdef DiskSliceSize
DiskSpanning=yes
#ifndef NO_SLICES
DiskSliceSize={#DiskSliceSize}
#else
DiskSize={#DiskSliceSize}
#endif
#endif
#ifdef Password
Password={#Password}
Encryption={#Encryption}
#endif

#ifndef NO_COMPONENTS
[Types]
Name: "full"; Description: "Full installation"; Flags: iscustom

[Components]
Name: "main"; Description: "Main files"; Types: full
Name: "extra"; Description: "Extra files"; Types: full
Name: "extra\sub"; Description: "Extra subcomponent"; Types: full
#endif

#ifndef NO_LANGUAGES
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#ifndef NO_GERMAN
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
#else
Name: "de"; MessagesFile: "compiler:Default.isl"
#endif
#endif

[Files]
Source: "payload\readme.txt"; DestDir: "{app}"
Source: "payload\empty.dat"; DestDir: "{app}"
Source: "payload\docs\manual.txt"; DestDir: "{app}\docs"
; 3 MB of random data, left out of the installers of the old compilers.
#ifndef SMALL_CORPUS
Source: "payload\data\random.bin"; DestDir: "{app}\data"
#endif
Source: "payload\data\nested\deeper\leaf.txt"; DestDir: "{app}\data\nested\deeper"
; One source file installed to two destinations: Inno Setup stores the data once.
Source: "payload\shared.txt"; DestDir: "{app}"
Source: "payload\shared.txt"; DestDir: "{app}\copy"
#ifndef NO_CODE
; A destination computed by a [Code] function.
Source: "payload\code.txt"; DestDir: "{code:GetDestDir}"
#endif
; An x86 executable: compressed chunks store it with the call instruction filter.
Source: "payload\bin\calls.exe"; DestDir: "{app}\bin"
; Names in Windows-1252 work in ANSI installers too; other names need a
; Unicode compiler.
#ifndef NO_RECURSE
Source: "payload\latin1\*"; DestDir: "{app}\latin1"; Flags: recursesubdirs
#else
Source: "payload\latin1\*"; DestDir: "{app}\latin1"
#endif
#ifdef DiskSliceSize
; Data for several slices.
Source: "payload\spanned\big.bin"; DestDir: "{app}\spanned"
#endif
#ifdef UNICODE
Source: "payload\unicode\*"; DestDir: "{app}\unicode"; Flags: recursesubdirs
#endif

; Entries with the same destination (ignoring case) get leading folders for
; the properties that differ: architecture, component, language, then a number.
; Because the architecture differs for some destinations, every entry with an
; architecture gets its folder, also when its destination is unique.
#ifndef NO_CHECK
; expect: {x64}\{app}\collide\unique\a.dll
Source: "payload\collide\unique-x64.dll"; DestDir: "{app}\collide\unique"; DestName: "a.dll"; Check: Is64BitInstallMode
; expect: {x86}\{app}\collide\arch\a.dll
Source: "payload\collide\arch-x86.dll"; DestDir: "{app}\collide\arch"; DestName: "a.dll"; Check: not Is64BitInstallMode
; expect: {x64}\{app}\collide\arch\a.dll
Source: "payload\collide\arch-x64.dll"; DestDir: "{app}\collide\arch"; DestName: "a.dll"; Check: Is64BitInstallMode
#endif
#ifndef NO_COMPONENTS
; expect: {#main}\{app}\collide\comp\a.dll
Source: "payload\collide\comp-main.dll"; DestDir: "{app}\collide\comp"; DestName: "a.dll"; Components: main
; expect: {#extra}\{app}\collide\comp\a.dll
Source: "payload\collide\comp-extra.dll"; DestDir: "{app}\collide\comp"; DestName: "a.dll"; Components: extra
; expect: {app}\collide\comp\a.dll
Source: "payload\collide\comp-none.dll"; DestDir: "{app}\collide\comp"; DestName: "a.dll"
#endif
; expect: {app}\collide\three\a.dll
Source: "payload\collide\three-1.dll"; DestDir: "{app}\collide\three"; DestName: "a.dll"
; expect: {$1}\{app}\collide\three\a.dll
Source: "payload\collide\three-2.dll"; DestDir: "{app}\collide\three"; DestName: "a.dll"
; expect: {$2}\{app}\collide\Three\A.DLL
Source: "payload\collide\three-3.dll"; DestDir: "{app}\collide\Three"; DestName: "A.DLL"
#ifndef NO_ARM64
; expect: {x64}\{app}\collide\win\a.dll
Source: "payload\collide\win-x64.dll"; DestDir: "{app}\collide\win"; DestName: "a.dll"; Check: IsWin64 and not IsARM64
; expect: {x86}\{app}\collide\win\a.dll
Source: "payload\collide\win-x86.dll"; DestDir: "{app}\collide\win"; DestName: "a.dll"; Check: not IsWin64 and not IsARM64
; expect: {arm64}\{app}\collide\win\a.dll
Source: "payload\collide\win-arm64.dll"; DestDir: "{app}\collide\win"; DestName: "a.dll"; Check: IsARM64
#endif
#ifndef NO_CHECK
; expect: {x86}\{app}\collide\flags\a.dll
Source: "payload\collide\flags-32.dll"; DestDir: "{app}\collide\flags"; DestName: "a.dll"; Flags: 32bit
; expect: {x64}\{app}\collide\flags\a.dll
Source: "payload\collide\flags-64.dll"; DestDir: "{app}\collide\flags"; DestName: "a.dll"; Flags: 64bit
#endif
#ifndef NO_LANGUAGES
; expect: {@en}\{app}\collide\lang\a.txt
Source: "payload\collide\lang-en.txt"; DestDir: "{app}\collide\lang"; DestName: "a.txt"; Languages: en
; expect: {@de}\{app}\collide\lang\a.txt
Source: "payload\collide\lang-de.txt"; DestDir: "{app}\collide\lang"; DestName: "a.txt"; Languages: de
#endif
#ifndef NO_CHECK
; Folder names replace "\" with "_"; a Check that calls a function with
; arguments gives no architecture.
; expect: {x64}\{#main}\{app}\collide\mixed\a.dll
Source: "payload\collide\mixed-1.dll"; DestDir: "{app}\collide\mixed"; DestName: "a.dll"; Components: main; Check: Is64BitInstallMode
; expect: {x64}\{#main}\{$1}\{app}\collide\mixed\a.dll
Source: "payload\collide\mixed-2.dll"; DestDir: "{app}\collide\mixed"; DestName: "a.dll"; Components: main; Check: IsWin64()
; expect: {x86}\{#extra_sub}\{app}\collide\mixed\a.dll
Source: "payload\collide\mixed-3.dll"; DestDir: "{app}\collide\mixed"; DestName: "a.dll"; Components: extra\sub; Check: not Is64BitInstallMode
; expect: {#main}\{app}\collide\mixed\a.dll
Source: "payload\collide\mixed-4.dll"; DestDir: "{app}\collide\mixed"; DestName: "a.dll"; Components: main; Check: IsWin64 and FileExists('x')
#endif

#ifndef NO_CODE
[Code]
function GetDestDir(Param: String): String;
begin
  Result := ExpandConstant('{app}\code');
end;
#endif
