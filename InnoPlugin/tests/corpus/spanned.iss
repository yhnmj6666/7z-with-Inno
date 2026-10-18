; Corpus variant: LZMA2, solid, disk spanning: the file data is in the slices
; spanned-1.bin, spanned-2.bin, ... The first slice must also hold Setup and
; its headers (about 2.2 MB for Inno Setup 7.1.0).
#define Compression "lzma2"
#define Solid "yes"
#define DiskSliceSize "3000000"
#include "common.iss"
