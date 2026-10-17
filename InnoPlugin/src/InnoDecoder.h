// InnoDecoder.h
// Native data pipeline: decrypts and decompresses Inno Setup chunks with
// 7-Zip streams and decoders. The setup of the decoders follows 7-Zip's NSIS
// handler (CPP/7zip/Archive/Nsis/NsisDecode.cpp).

#ifndef ZIP7_INC_INNO_DECODER_H
#define ZIP7_INC_INNO_DECODER_H

#include "C/Lzma2Dec.h"
#include "C/LzmaDec.h"

#include "CPP/Common/MyBuffer.h"
#include "CPP/Common/MyCom.h"
#include "CPP/7zip/IStream.h"

#include "inno/crypto/arc4.hpp"
#include "inno/stream/chunk.hpp"

#include <string>

namespace NArchive {
namespace NInno {

// Inno Setup LZMA1 (5 bytes of properties, then raw LZMA data) or LZMA2
// (1 byte dictionary property, then LZMA2 data), decoded with C/LzmaDec.c and
// C/Lzma2Dec.c. Read() returns S_FALSE on a data error and a short read at the
// end of the stream (end marker or end of input).
Z7_CLASS_IMP_NOQIB_1(
  CLzmaInStream
  , ISequentialInStream
)
  CMyComPtr<ISequentialInStream> _stream;
  bool _lzma2;
  bool _initialized;
  bool _finished;
  bool _inEof;
  size_t _inPos;
  size_t _inLim;
  CByteBuffer _inBuf;
  CLzmaDec _lzmaDec;
  CLzma2Dec _lzma2Dec;

  HRESULT InitDecoder();
public:
  CLzmaInStream(ISequentialInStream *stream, bool lzma2);
  ~CLzmaInStream();
};

// ARC4 decryption of the data read from the underlying stream.
Z7_CLASS_IMP_NOQIB_1(
  CArc4InStream
  , ISequentialInStream
)
  CMyComPtr<ISequentialInStream> _stream;
  crypto::arc4 _arc4;
public:
  CArc4InStream(ISequentialInStream *stream, const char *key, size_t keySize);
};

// Decompressed data of one chunk, read sequentially. Files are located by
// their offset in the decompressed data: going forward skips data, going
// backwards reopens the chunk.
class CChunkDecoder
{
  bool _isOpen;
  stream::chunk _chunk;
  UInt64 _pos;
  CMyComPtr<ISequentialInStream> _decoded;
  CByteBuffer _skipBuf;

public:
  CChunkDecoder(): _isOpen(false), _pos(0) {}

  bool IsOpen(const stream::chunk &chunk) const { return _isOpen && _chunk == chunk; }
  UInt64 GetPos() const { return _pos; }
  void Close();

  // Opens the chunk stored at dataOffset + chunk.offset in stream.
  // Returns S_FALSE if the chunk is damaged, E_NOTIMPL if its compression
  // method is not supported.
  HRESULT Open(IInStream *stream, UInt64 dataOffset, const stream::chunk &chunk,
      const std::string &password);

  // Reads up to size bytes; fewer only at the end of the data. Returns
  // S_FALSE on a data error. The decoder is closed after any error.
  HRESULT Read(Byte *data, size_t &size);

  // Skips forward to the position pos (pos >= GetPos()). Returns S_FALSE if
  // the data ends before pos or on a data error.
  HRESULT SkipTo(UInt64 pos);
};

}}

#endif
