; Defines Compression: "lzma2", or for the old compilers without LZMA2 (see
; FEATURES in run_tests.py) "lzma", or "zip" without LZMA either.
#ifndef NO_LZMA2
#define Compression "lzma2"
#else
#ifndef NO_LZMA
#define Compression "lzma"
#else
#define Compression "zip"
#endif
#endif
