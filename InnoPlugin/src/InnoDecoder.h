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
#include "CPP/7zip/Common/LimitedStreams.h"

#include "inno/crypto/arc4.hpp"
#include "inno/crypto/xchacha20.hpp"
#include "inno/stream/chunk.hpp"

#include <string>
#include <vector>

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

// XChaCha20 decryption (Inno Setup 6.4+) of the data read from the underlying
// stream. key is the derived key followed by the base nonce.
Z7_CLASS_IMP_NOQIB_1(
  CXChaCha20InStream
  , ISequentialInStream
)
  CMyComPtr<ISequentialInStream> _stream;
  crypto::xchacha20 _cipher;
public:
  CXChaCha20InStream(ISequentialInStream *stream, const std::string &key, const stream::chunk &chunk);
};

// A slice file of a disk-spanning installer ("setup-1.bin"): a header of 8
// bytes of magic and the size of the slice (32-bit, or 64-bit from Inno
// Setup 6.5.2 on), then data up to that size.
struct CSlice
{
  CMyComPtr<IInStream> Stream; // NULL if the slice is missing
  UInt64 Size;                 // size from the slice header
  unsigned HeaderSize;
  CSlice(): Size(0), HeaderSize(0) {}
};

// Data of a disk-spanning installer from a position in a slice on: reading
// continues after the header of the next slice at the end of each slice. The
// data ends early at a missing or truncated slice.
Z7_CLASS_IMP_NOQIB_1(
  CSliceInStream
  , ISequentialInStream
)
  const std::vector<CSlice> *_slices;
  size_t _slice;
  UInt64 _pos;
public:
  bool MissingSlice; // the data ended at a missing slice
  bool Truncated;    // the data ended at the end of a slice file
  CSliceInStream(const std::vector<CSlice> &slices, size_t slice, UInt64 pos):
      _slices(&slices), _slice(slice), _pos(pos), MissingSlice(false), Truncated(false) {}
};

// Decompressed data of one chunk, read sequentially. Files are located by
// their offset in the decompressed data: going forward skips data, going
// backwards reopens the chunk.
class CChunkDecoder
{
  bool _isOpen;
  stream::chunk _chunk;
  UInt64 _pos;
  UInt64 _packSizeClosed;
  CMyComPtr<ISequentialInStream> _decoded;
  CLimitedSequentialInStream *_limitedSpec;
  CMyComPtr<ISequentialInStream> _limited;
  CByteBuffer _skipBuf;

public:
  CChunkDecoder(): _isOpen(false), _pos(0), _packSizeClosed(0), _limitedSpec(NULL) {}

  bool IsOpen(const stream::chunk &chunk) const { return _isOpen && _chunk == chunk; }
  UInt64 GetPos() const { return _pos; }
  // Total number of bytes read from the archive for all chunks so far.
  UInt64 GetPackSize() const
    { return _packSizeClosed + (_limitedSpec ? _limitedSpec->GetSize() : 0); }
  void Close();

  // Opens the chunk that starts at the current position of stream. key is
  // the key of encrypted chunks (setup::info::get_key).
  // Returns S_FALSE if the chunk is damaged, E_NOTIMPL if its compression
  // or encryption method is not supported.
  HRESULT Open(ISequentialInStream *stream, const stream::chunk &chunk, const std::string &key);

  // Reads up to size bytes; fewer only at the end of the data. Returns
  // S_FALSE on a data error. The decoder is closed after any error.
  HRESULT Read(Byte *data, size_t &size);

  // Skips forward to the position pos (pos >= GetPos()). Returns S_FALSE if
  // the data ends before pos or on a data error.
  HRESULT SkipTo(UInt64 pos);
};

}}

#endif
