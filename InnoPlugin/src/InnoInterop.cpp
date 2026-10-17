// InnoInterop.cpp

#include "StdAfx.h"

#include <string.h>

#include "CPP/7zip/Common/StreamUtils.h"

#include "InnoInterop.h"

namespace NArchive {
namespace NInno {

static const size_t kBufSize = (size_t)1 << 16;

CInStreamBuf::CInStreamBuf(IInStream *stream):
    _stream(stream),
    _buf(kBufSize),
    _bufPos(0)
{
  setg(BufStart(), BufStart(), BufStart());
}

std::streambuf::pos_type CInStreamBuf::SeekTo(UInt64 pos)
{
  const UInt64 bufSize = (UInt64)(egptr() - eback());
  if (pos >= _bufPos && pos - _bufPos <= bufSize)
    setg(eback(), eback() + (size_t)(pos - _bufPos), egptr());
  else
  {
    // The next read starts at pos.
    _bufPos = pos;
    setg(BufStart(), BufStart(), BufStart());
  }
  return pos_type(off_type(pos));
}

std::streambuf::pos_type CInStreamBuf::seekoff(off_type off, std::ios_base::seekdir dir,
    std::ios_base::openmode which)
{
  const pos_type kError = pos_type(off_type(-1));
  if ((which & std::ios_base::in) == 0)
    return kError;
  UInt64 base;
  switch (dir)
  {
    case std::ios_base::beg:
      base = 0;
      break;
    case std::ios_base::cur:
      base = GetCurPos();
      break;
    case std::ios_base::end:
      if (_stream->Seek(0, STREAM_SEEK_END, &base) != S_OK)
        return kError;
      break;
    default:
      return kError;
  }
  const UInt64 kMaxPos = (UInt64)1 << 62;
  UInt64 pos;
  if (off < 0)
  {
    const UInt64 sub = (UInt64)0 - (UInt64)off;
    if (sub > base)
      return kError;
    pos = base - sub;
  }
  else
  {
    if (base > kMaxPos || (UInt64)off > kMaxPos - base)
      return kError;
    pos = base + (UInt64)off;
  }
  return SeekTo(pos);
}

std::streambuf::pos_type CInStreamBuf::seekpos(pos_type pos, std::ios_base::openmode which)
{
  return seekoff(off_type(pos), std::ios_base::beg, which);
}

std::streambuf::int_type CInStreamBuf::underflow()
{
  if (gptr() < egptr())
    return traits_type::to_int_type(*gptr());
  _bufPos = GetCurPos();
  setg(BufStart(), BufStart(), BufStart());
  if (InStream_SeekSet(_stream, _bufPos) != S_OK)
    return traits_type::eof();
  size_t size = kBufSize;
  ReadStream(_stream, _buf, &size);
  if (size == 0)
    return traits_type::eof();
  setg(BufStart(), BufStart(), BufStart() + size);
  return traits_type::to_int_type(*gptr());
}

std::streamsize CInStreamBuf::xsgetn(char_type *s, std::streamsize count)
{
  std::streamsize done = 0;
  while (done < count)
  {
    const size_t rem = (size_t)(count - done);
    const size_t avail = (size_t)(egptr() - gptr());
    if (avail != 0)
    {
      const size_t size = avail < rem ? avail : rem;
      memcpy(s + done, gptr(), size);
      gbump((int)size);
      done += (std::streamsize)size;
      continue;
    }
    if (rem >= kBufSize)
    {
      // Large reads bypass the buffer.
      _bufPos = GetCurPos();
      setg(BufStart(), BufStart(), BufStart());
      if (InStream_SeekSet(_stream, _bufPos) != S_OK)
        break;
      size_t size = rem;
      ReadStream(_stream, s + done, &size);
      _bufPos += size;
      done += (std::streamsize)size;
      break;
    }
    if (traits_type::eq_int_type(underflow(), traits_type::eof()))
      break;
  }
  return done;
}

}}
