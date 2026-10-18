// InnoBufferDecoder.h
// Decoders for the setup header blocks, which are decompressed into memory.

#ifndef ZIP7_INC_INNO_BUFFER_DECODER_H
#define ZIP7_INC_INNO_BUFFER_DECODER_H

#include "C/7zTypes.h"

#include <string>

namespace NArchive {
namespace NInno {

// Setup header blocks that decompress to more than this fail, so that a
// crafted installer cannot make the handler allocate memory without bound
// while it is opened. Real installers stay far below.
const size_t kMaxHeaderBlockSize = (size_t)1 << 28;

// Decodes a complete zlib stream (header, deflate data, Adler-32) held in
// memory. Returns false on a data error or above kMaxHeaderBlockSize.
bool DecodeZlibBuffer(const Byte *data, size_t size, std::string &out);

// Decodes Inno Setup LZMA1 data held in memory: the 5 bytes of LZMA
// properties (no size field), then raw LZMA data. Returns false on a data
// error or above kMaxHeaderBlockSize.
bool DecodeLzma1Buffer(const Byte *data, size_t size, std::string &out);

}}

#endif
