; Corpus variant for the ANSI compilers (CODEPAGE_VARIANTS in run_tests.py):
; the only language uses code page 1251 (Cyrillic), so the setup headers are
; decoded with it. The compiler reads the names in payload\latin1 in Wine's
; ANSI code page (1252) and stores those bytes, so they are listed with their
; meaning in code page 1251: e acute (0xE9) becomes the Cyrillic short i.
#include "compression.iss"

[Setup]
AppName=InnoPluginTest
AppVersion=1.0
DefaultDirName={sd}\InnoPluginTest
DisableProgramGroupPage=yes
#ifndef NO_COMPRESSION
Compression={#Compression}
#endif

[Languages]
Name: "ru"; MessagesFile: "compiler:Default.isl"

[LangOptions]
LanguageCodePage=1251

[Files]
Source: "payload\readme.txt"; DestDir: "{app}"
Source: "payload\latin1\*"; DestDir: "{app}\latin1"; Flags: recursesubdirs
