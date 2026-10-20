; Corpus variant: LZMA2 (older compilers: see compression.iss), solid, disk
; spanning: the file data is in the slices spanned-1.bin, spanned-2.bin, ...
; (spanned.1, spanned.2, ... before Inno Setup 2.0). The first slice must also
; hold Setup and its headers (about 2.2 MB for Inno Setup 7.1.0).
#include "compression.iss"
#define Solid "yes"
#define DiskSliceSize "3000000"
#include "common.iss"
