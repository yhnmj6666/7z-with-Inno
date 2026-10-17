// InnoBufferDecoder.h
// Decoders for the setup header blocks, which are decompressed into memory.

#ifndef ZIP7_INC_INNO_BUFFER_DECODER_H
#define ZIP7_INC_INNO_BUFFER_DECODER_H

#include "C/7zTypes.h"

#include <string>

namespace NArchive {
namespace NInno {

// Decodes a complete zlib stream (header, deflate data, Adler-32) held in
// memory. Returns false on a data error.
bool DecodeZlibBuffer(const Byte *data, size_t size, std::string &out);

// Decodes Inno Setup LZMA1 data held in memory: the 5 bytes of LZMA
// properties (no size field), then raw LZMA data. Returns false on a data
// error.
bool DecodeLzma1Buffer(const Byte *data, size_t size, std::string &out);

}}

#endif
