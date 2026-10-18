; Corpus variant: LZMA2, not solid, files and setup headers encrypted with a
; password (Encryption=full, Inno Setup 6.5 and later).
#define Compression "lzma2"
#define Solid "no"
#define Password "inno-test-password"
#define Encryption "full"
#include "common.iss"
