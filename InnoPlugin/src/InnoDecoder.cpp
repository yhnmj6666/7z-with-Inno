// InnoDecoder.cpp

#include "StdAfx.h"

#include "C/Alloc.h"

#include "CPP/7zip/Common/LimitedStreams.h"
#include "CPP/7zip/Common/StreamObjects.h"
#include "CPP/7zip/Common/StreamUtils.h"
#include "CPP/7zip/Compress/BZip2Decoder.h"
#include "CPP/7zip/Compress/DeflateDecoder.h"
#include "CPP/7zip/Compress/ZlibDecoder.h"

#include "InnoBufferDecoder.h"
#include "InnoDecoder.h"

#include "inno/crypto/hasher.hpp"

#include <string.h>

namespace NArchive {
namespace NInno {

static const size_t kLzmaInBufSize = 1 << 16;
static const size_t kSkipBufSize = 1 << 16;
static const Byte kChunkMagic[4] = { 'z', 'l', 'b', 0x1a };

bool DecodeZlibBuffer(const Byte *data, size_t size, std::string &out)
{
  CMyComPtr2_Create<IInStream, CBufInStream> inStream;
  inStream->Init(data, size);
  CMyComPtr2_Create<ISequentialOutStream, CDynBufSeqOutStream> outStream;
  outStream->Init();
  CMyComPtr2_Create<ICompressCoder, NCompress::NZlib::CDecoder> decoder;
  ICompressCoder *coder = decoder;
  const HRESULT res = coder->Code(inStream, outStream, NULL, NULL, NULL);
  if (res != S_OK)
    return false;
  out.assign((const char *)outStream->GetBuffer(), outStream->GetSize());
  return true;
}

bool DecodeLzma1Buffer(const Byte *data, size_t size, std::string &out)
{
  out.clear();
  if (size < LZMA_PROPS_SIZE)
    return false;
  CLzmaDec dec;
  LzmaDec_CONSTRUCT(&dec)
  if (LzmaDec_Allocate(&dec, data, LZMA_PROPS_SIZE, &g_Alloc) != SZ_OK)
    return false;
  LzmaDec_Init(&dec);
  data += LZMA_PROPS_SIZE;
  size -= LZMA_PROPS_SIZE;
  bool ok = true;
  size_t outPos = 0;
  for (;;)
  {
    const size_t kStep = 1 << 16;
    out.resize(outPos + kStep);
    SizeT outLen = kStep;
    SizeT inLen = size;
    ELzmaStatus status;
    const SRes res = LzmaDec_DecodeToBuf(&dec, (Byte *)&out[outPos], &outLen,
        data, &inLen, LZMA_FINISH_ANY, &status);
    data += inLen;
    size -= inLen;
    outPos += outLen;
    if (res != SZ_OK)
    {
      ok = false;
      break;
    }
    if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      break;
    if (inLen == 0 && outLen == 0)
    {
      // The input is exhausted: the stream is truncated unless it may end here.
      ok = (size == 0 && status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK);
      break;
    }
  }
  LzmaDec_Free(&dec, &g_Alloc);
  out.resize(outPos);
  return ok;
}


CLzmaInStream::CLzmaInStream(ISequentialInStream *stream, bool lzma2):
    _stream(stream),
    _lzma2(lzma2),
    _initialized(false),
    _finished(false),
    _inEof(false),
    _inPos(0),
    _inLim(0)
{
  LzmaDec_CONSTRUCT(&_lzmaDec)
  Lzma2Dec_CONSTRUCT(&_lzma2Dec)
}

CLzmaInStream::~CLzmaInStream()
{
  if (_lzma2)
    Lzma2Dec_Free(&_lzma2Dec, &g_Alloc);
  else
    LzmaDec_Free(&_lzmaDec, &g_Alloc);
}

HRESULT CLzmaInStream::InitDecoder()
{
  _inBuf.Alloc(kLzmaInBufSize);
  if (_lzma2)
  {
    Byte prop;
    RINOK(ReadStream_FALSE(_stream, &prop, 1))
    const SRes res = Lzma2Dec_Allocate(&_lzma2Dec, prop, &g_Alloc);
    if (res == SZ_ERROR_MEM)
      return E_OUTOFMEMORY;
    if (res != SZ_OK)
      return S_FALSE;
    Lzma2Dec_Init(&_lzma2Dec);
  }
  else
  {
    Byte props[LZMA_PROPS_SIZE];
    RINOK(ReadStream_FALSE(_stream, props, LZMA_PROPS_SIZE))
    const SRes res = LzmaDec_Allocate(&_lzmaDec, props, LZMA_PROPS_SIZE, &g_Alloc);
    if (res == SZ_ERROR_MEM)
      return E_OUTOFMEMORY;
    if (res != SZ_OK)
      return S_FALSE;
    LzmaDec_Init(&_lzmaDec);
  }
  _initialized = true;
  return S_OK;
}

Z7_COM7F_IMF(CLzmaInStream::Read(void *data, UInt32 size, UInt32 *processedSize))
{
  if (processedSize)
    *processedSize = 0;
  if (!_initialized)
  {
    RINOK(InitDecoder())
  }
  UInt32 total = 0;
  while (size != 0 && !_finished)
  {
    if (_inPos == _inLim && !_inEof)
    {
      size_t len = kLzmaInBufSize;
      RINOK(ReadStream(_stream, _inBuf, &len))
      _inPos = 0;
      _inLim = len;
      if (len == 0)
        _inEof = true;
    }
    SizeT outLen = size;
    SizeT inLen = _inLim - _inPos;
    ELzmaStatus status;
    const SRes res = _lzma2 ?
        Lzma2Dec_DecodeToBuf(&_lzma2Dec, (Byte *)data, &outLen,
            _inBuf + _inPos, &inLen, LZMA_FINISH_ANY, &status) :
        LzmaDec_DecodeToBuf(&_lzmaDec, (Byte *)data, &outLen,
            _inBuf + _inPos, &inLen, LZMA_FINISH_ANY, &status);
    _inPos += inLen;
    data = (Byte *)data + outLen;
    size -= (UInt32)outLen;
    total += (UInt32)outLen;
    if (processedSize)
      *processedSize = total;
    if (res != SZ_OK)
      return S_FALSE;
    if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      _finished = true;
    else if (inLen == 0 && outLen == 0)
    {
      if (_inEof)
        break;
      if (_inPos != _inLim)
        return S_FALSE;
    }
  }
  return S_OK;
}


CArc4InStream::CArc4InStream(ISequentialInStream *stream, const char *key, size_t keySize):
    _stream(stream)
{
  _arc4.init(key, keySize);
  _arc4.discard(1000);
}

Z7_COM7F_IMF(CArc4InStream::Read(void *data, UInt32 size, UInt32 *processedSize))
{
  UInt32 processed = 0;
  const HRESULT res = _stream->Read(data, size, &processed);
  _arc4.crypt((const char *)data, (char *)data, processed);
  if (processedSize)
    *processedSize = processed;
  return res;
}


void CChunkDecoder::Close()
{
  _isOpen = false;
  _decoded.Release();
  if (_limitedSpec)
  {
    _packSizeClosed += _limitedSpec->GetSize();
    _limitedSpec = NULL;
    _limited.Release();
  }
  _pos = 0;
}

HRESULT CChunkDecoder::Open(IInStream *stream, UInt64 dataOffset, const stream::chunk &chunk,
    const std::string &password)
{
  Close();

  RINOK(InStream_SeekSet(stream, dataOffset + chunk.offset))
  Byte magic[sizeof(kChunkMagic)];
  RINOK(ReadStream_FALSE(stream, magic, sizeof(magic)))
  if (memcmp(magic, kChunkMagic, sizeof(magic)) != 0)
    return S_FALSE;

  char salt[8];
  if (chunk.encryption != stream::Plaintext)
  {
    RINOK(ReadStream_FALSE(stream, salt, sizeof(salt)))
  }

  _limitedSpec = new CLimitedSequentialInStream;
  _limited = _limitedSpec;
  _limitedSpec->SetStream(stream);
  _limitedSpec->Init(chunk.size);
  CMyComPtr<ISequentialInStream> input = _limited;

  if (chunk.encryption != stream::Plaintext)
  {
    const bool sha1 = (chunk.encryption == stream::ARC4_SHA1);
    crypto::hasher hasher(sha1 ? crypto::SHA1 : crypto::MD5);
    hasher.update(salt, sizeof(salt));
    hasher.update(password.c_str(), password.length());
    const crypto::checksum key = hasher.finalize();
    input = new CArc4InStream(input, sha1 ? key.sha1 : key.md5,
        sha1 ? sizeof(key.sha1) : sizeof(key.md5));
  }

  CMyComPtr<ISequentialInStream> decoded;
  switch (chunk.compression)
  {
    case stream::Stored:
      decoded = input;
      break;
    case stream::Zlib:
    {
      // The zlib header is checked here; the deflate decoder works in pull
      // mode, which NCompress::NZlib::CDecoder does not provide. The Adler-32
      // trailer is not checked: every file has its own checksum.
      Byte header[2];
      RINOK(ReadStream_FALSE(input, header, sizeof(header)))
      if (!NCompress::NZlib::IsZlib(header))
        return S_FALSE;
      decoded = new NCompress::NDeflate::NDecoder::CCOMCoder;
      break;
    }
    case stream::BZip2:
      decoded = new NCompress::NBZip2::CDecoder;
      break;
    case stream::LZMA1:
    case stream::LZMA2:
      decoded = new CLzmaInStream(input, chunk.compression == stream::LZMA2);
      break;
    default:
      return E_NOTIMPL;
  }

  if (chunk.compression == stream::Zlib || chunk.compression == stream::BZip2)
  {
    CMyComPtr<ICompressSetInStream> setInStream;
    decoded.QueryInterface(IID_ICompressSetInStream, &setInStream);
    if (!setInStream)
      return E_NOTIMPL;
    RINOK(setInStream->SetInStream(input))
    CMyComPtr<ICompressSetOutStreamSize> setOutStreamSize;
    decoded.QueryInterface(IID_ICompressSetOutStreamSize, &setOutStreamSize);
    if (setOutStreamSize)
    {
      RINOK(setOutStreamSize->SetOutStreamSize(NULL))
    }
  }

  _decoded = decoded;
  _chunk = chunk;
  _pos = 0;
  _isOpen = true;
  return S_OK;
}

HRESULT CChunkDecoder::Read(Byte *data, size_t &size)
{
  if (!_isOpen)
  {
    size = 0;
    return S_FALSE;
  }
  const HRESULT res = ReadStream(_decoded, data, &size);
  _pos += size;
  if (res != S_OK)
    Close();
  return res;
}

HRESULT CChunkDecoder::SkipTo(UInt64 pos)
{
  if (pos < _pos)
    return E_FAIL;
  if (pos != _pos && _skipBuf.Size() == 0)
    _skipBuf.Alloc(kSkipBufSize);
  while (pos != _pos)
  {
    const UInt64 rem = pos - _pos;
    size_t size = rem < kSkipBufSize ? (size_t)rem : kSkipBufSize;
    const size_t requested = size;
    RINOK(Read(_skipBuf, size))
    if (size != requested)
      return S_FALSE;
  }
  return S_OK;
}

}}
