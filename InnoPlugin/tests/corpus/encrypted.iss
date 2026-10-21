; Corpus variant: LZMA2 (older compilers: see compression.iss), solid, files
; encrypted with a password (ARC4 before Inno Setup 6.4, XChaCha20 from 6.4).
#include "compression.iss"
#define Solid "yes"
#define Password "inno-test-password"
#define Encryption "yes"
#include "common.iss"
