// InnoInterop.h
// std::streambuf over a 7-Zip IInStream, used by the innoextract parser to
// read the setup loader and the setup headers.

#ifndef ZIP7_INC_INNO_INTEROP_H
#define ZIP7_INC_INNO_INTEROP_H

#include "CPP/Common/MyBuffer.h"
#include "CPP/Common/MyCom.h"
#include "CPP/7zip/IStream.h"

#include <streambuf>

namespace NArchive {
namespace NInno {

// Reads through a 64 KiB buffer. Seeks inside the buffered data do not access
// the stream. A seek or read error ends the data like the end of the stream;
// GetError() returns the first one.
class CInStreamBuf: public std::streambuf
{
  CMyComPtr<IInStream> _stream;
  CByteBuffer _buf;
  UInt64 _bufPos; // stream position of eback()
  HRESULT _error;

  char *BufStart() { return (char *)(Byte *)_buf; }
  UInt64 GetCurPos() const { return _bufPos + (UInt64)(gptr() - eback()); }
  pos_type SeekTo(UInt64 pos);
  bool SetError(HRESULT res)
  {
    if (res == S_OK)
      return false;
    if (_error == S_OK)
      _error = res;
    return true;
  }

protected:
  pos_type seekoff(off_type off, std::ios_base::seekdir dir,
      std::ios_base::openmode which) override;
  pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;
  int_type underflow() override;
  std::streamsize xsgetn(char_type *s, std::streamsize count) override;

public:
  explicit CInStreamBuf(IInStream *stream);
  HRESULT GetError() const { return _error; }
};

}}

#endif
