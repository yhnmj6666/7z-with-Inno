; Corpus variant: LZMA2, solid, files encrypted with a password
; (ARC4 before Inno Setup 6.4, XChaCha20 from 6.4).
#define Compression "lzma2"
#define Solid "yes"
#define Password "inno-test-password"
#define Encryption "yes"
#include "common.iss"
