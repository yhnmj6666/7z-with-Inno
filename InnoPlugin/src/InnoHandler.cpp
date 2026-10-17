// InnoHandler.cpp

#include "StdAfx.h"

#include "CPP/Windows/PropVariant.h"

#include "CPP/7zip/Common/ProgressUtils.h"
#include "CPP/7zip/Common/StreamUtils.h"

#include "InnoDecoder.h"
#include "InnoHandler.h"
#include "InnoPe.h"

#include "inno/crypto/hasher.hpp"
#include "inno/stream/exefilter.hpp"

#include <algorithm>
#include <new>

using namespace NWindows;

namespace NArchive {
namespace NInno {

static const Byte kProps[] =
{
  kpidPath,
  kpidSize,
  kpidMTime,
  kpidAttrib,
  kpidCRC,
  kpidMethod,
  kpidSolid,
  kpidEncrypted
};

static const Byte kArcProps[] =
{
  kpidMethod,
  kpidSolid,
  kpidPhySize,
  kpidHeadersSize,
  kpidComment
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps

static const size_t kBufSize = (size_t)1 << 16;
// Decoded data of a file that is extracted to several items is kept in memory
// up to this size; larger files are decoded again for each item.
static const UInt64 kMaxCacheSize = (UInt64)1 << 26;

// Hosts compile IInArchive methods as non-throwing, so no C++ exception may
// escape into the 7-Zip process.

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 * /* maxCheckStartPosition */,
    IArchiveOpenCallback *callback))
{
  try
  {
    Close();
    if (callback)
    {
      Z7_DECL_CMyComPtr_QI_FROM(IArchiveOpenVolumeCallback, volumeCallback, callback)
      if (volumeCallback)
      {
        NCOM::CPropVariant prop;
        if (volumeCallback->GetProperty(kpidMTime, &prop) == S_OK && prop.vt == VT_FILETIME)
        {
          _mtime = prop.filetime;
          _mtimeDefined = true;
        }
      }
    }
    const HRESULT res = _arc.Open(stream);
    if (res != S_OK)
      Close();
    return res;
  }
  catch (const std::bad_alloc &)
  {
    Close();
    return E_OUTOFMEMORY;
  }
  catch (...)
  {
    Close();
    return S_FALSE;
  }
}

Z7_COM7F_IMF(CHandler::Close())
{
  try
  {
    _arc.Clear();
  }
  catch (...) {}
  _mtimeDefined = false;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = (UInt32)_arc.Items.size();
  return S_OK;
}

static bool GetFileTime(const setup::data_entry &entry, FILETIME &ft)
{
  // FILETIME of 1970-01-01, timestamps are in seconds since then.
  const UInt64 kUnixEpoch = (UInt64)0x19DB1DED53E8000;
  const UInt64 kMaxSeconds = ((UInt64)1 << 62) / 10000000;
  if (entry.timestamp < 0 || (UInt64)entry.timestamp > kMaxSeconds
      || entry.timestamp_nsec >= 1000000000)
    return false;
  const UInt64 v = (UInt64)entry.timestamp * 10000000 + entry.timestamp_nsec / 100 + kUnixEpoch;
  ft.dwLowDateTime = (DWORD)v;
  ft.dwHighDateTime = (DWORD)(v >> 32);
  return true;
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  try
  {
    if (index >= _arc.Items.size())
      return E_INVALIDARG;
    NCOM::CPropVariant prop;
    const CItem &item = _arc.Items[index];
    switch (propID)
    {
      case kpidPath: prop = item.Path; break;
      case kpidSize: prop = item.Size; break;
      case kpidMTime:
      {
        if (item.FileIndex < 0)
        {
          if (_mtimeDefined)
            prop = _mtime;
        }
        else
        {
          FILETIME ft;
          if (GetFileTime(_arc.GetDataEntry(item), ft))
            prop = ft;
        }
        break;
      }
      default:
      {
        if (item.FileIndex < 0)
          break;
        const setup::file_entry &file = _arc.Info.files[(size_t)item.FileIndex];
        const setup::data_entry &entry = _arc.GetDataEntry(item);
        switch (propID)
        {
          case kpidAttrib:
            if (file.attributes != 0)
              prop = (UInt32)file.attributes;
            break;
          case kpidCRC:
            if (entry.file.checksum.type == crypto::CRC32)
              prop = (UInt32)entry.file.checksum.crc32;
            break;
          case kpidMethod: prop = GetMethodString(entry); break;
          case kpidSolid: prop = (bool)_arc.SolidEntries[file.location]; break;
          case kpidEncrypted: prop = (entry.chunk.encryption != stream::Plaintext); break;
          default: break;
        }
        break;
      }
    }
    prop.Detach(value);
    return S_OK;
  }
  catch (const std::bad_alloc &)
  {
    return E_OUTOFMEMORY;
  }
  catch (...)
  {
    return E_FAIL;
  }
}

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  try
  {
    NCOM::CPropVariant prop;
    switch (propID)
    {
      case kpidMethod:
        if (!_arc.Method.IsEmpty())
          prop = _arc.Method;
        break;
      case kpidSolid: prop = _arc.IsSolid; break;
      case kpidPhySize:
        if (_arc.Stream)
          prop = _arc.PhySize;
        break;
      case kpidHeadersSize:
        if (_arc.Stream)
          prop = (UInt64)_arc.Info.header.header_size;
        break;
      case kpidComment:
        if (!_arc.Comment.IsEmpty())
          prop = _arc.Comment;
        break;
      case kpidWarning:
      {
        if (_arc.Warnings.empty())
          break;
        std::string s;
        for (size_t i = 0; i < _arc.Warnings.size(); i++)
        {
          if (i != 0)
            s += '\n';
          s += _arc.Warnings[i];
        }
        UString u;
        Utf8ToUnicode(s, u);
        prop = u;
        break;
      }
      default: break;
    }
    prop.Detach(value);
    return S_OK;
  }
  catch (const std::bad_alloc &)
  {
    return E_OUTOFMEMORY;
  }
  catch (...)
  {
    return E_FAIL;
  }
}

namespace {

struct CRequest
{
  UInt32 Index;
  UInt32 Location;
  UInt64 Planned; // progress units of this item
};

class CExtractor
{
  const CInArchive &_arc;
  CLocalProgress *_lpsSpec;
  CMyComPtr<ICompressProgressInfo> _lps;
  CByteBuffer _buf;

  bool _failed;
  stream::chunk _failedChunk;
  UInt64 _failedPos;
  Int32 _failedRes;

  UInt64 _unitStart;
  UInt64 _unitPlanned;
  UInt64 _unitProcessed;

  void SetFailed(const stream::chunk &chunk, UInt64 pos, Int32 res)
  {
    _failed = true;
    _failedChunk = chunk;
    _failedPos = pos;
    _failedRes = res;
  }
  Int32 GetDataErrorResult(const stream::chunk &chunk) const
  {
    return _arc.GetChunkEnd(chunk) > _arc.FileSize ?
        NExtract::NOperationResult::kUnexpectedEnd :
        NExtract::NOperationResult::kDataError;
  }
  HRESULT AddProcessed(size_t size)
  {
    _unitProcessed += size;
    return SetProgress();
  }

public:
  CChunkDecoder Decoder;

  CExtractor(const CInArchive &arc, IArchiveExtractCallback *callback):
      _arc(arc), _buf(kBufSize), _failed(false), _failedPos(0), _failedRes(0),
      _unitStart(0), _unitPlanned(0), _unitProcessed(0)
  {
    _lpsSpec = new CLocalProgress;
    _lps = _lpsSpec;
    _lpsSpec->Init(callback, false);
  }

  HRESULT SetProgress()
  {
    _lpsSpec->InSize = Decoder.GetPackSize();
    _lpsSpec->OutSize = _unitStart + (_unitProcessed < _unitPlanned ? _unitProcessed : _unitPlanned);
    return _lpsSpec->SetCur();
  }
  void BeginUnit(UInt64 planned)
  {
    _unitPlanned = planned;
    _unitProcessed = 0;
  }
  HRESULT EndUnit()
  {
    _unitStart += _unitPlanned;
    _unitPlanned = 0;
    _unitProcessed = 0;
    return SetProgress();
  }
  HRESULT WriteData(ISequentialOutStream *outStream, const std::string &data)
  {
    if (outStream)
      RINOK(WriteStream(outStream, data.data(), data.size()))
    return AddProcessed(data.size());
  }

  // Result that does not depend on the data, or -1.
  Int32 GetStaticResult(const setup::data_entry &entry) const
  {
    if (entry.file.size == 0)
      return -1;
    if (_arc.Offsets.data_offset == 0)
      return NExtract::NOperationResult::kUnavailable;
    if (entry.chunk.encryption != stream::Plaintext)
      return NExtract::NOperationResult::kUnsupportedMethod;
    if (entry.chunk.compression == stream::UnknownCompression)
      return NExtract::NOperationResult::kUnsupportedMethod;
    return -1;
  }

  HRESULT Decode(const setup::data_entry &entry, ISequentialOutStream *outStream,
      std::string *cache, Int32 &opRes);
};

HRESULT CExtractor::Decode(const setup::data_entry &entry, ISequentialOutStream *outStream,
    std::string *cache, Int32 &opRes)
{
  opRes = NExtract::NOperationResult::kOK;
  const stream::chunk &chunk = entry.chunk;
  const stream::file &file = entry.file;
  crypto::hasher hasher(file.checksum.type);
  if (file.size != 0)
  {
    if (_failed && _failedChunk == chunk && file.offset + file.size > _failedPos)
    {
      opRes = _failedRes;
      return S_OK;
    }
    if (!Decoder.IsOpen(chunk) || Decoder.GetPos() > file.offset)
    {
      const HRESULT res = Decoder.Open(_arc.Stream, _arc.Offsets.data_offset, chunk, std::string());
      if (res != S_OK)
      {
        Decoder.Close();
        if (res == S_FALSE)
          opRes = GetDataErrorResult(chunk);
        else if (res == E_NOTIMPL)
          opRes = NExtract::NOperationResult::kUnsupportedMethod;
        else
          return res;
        SetFailed(chunk, 0, opRes);
        return S_OK;
      }
    }
    Byte *buf = _buf;
    while (Decoder.GetPos() < file.offset)
    {
      const UInt64 rem = file.offset - Decoder.GetPos();
      size_t size = rem < kBufSize ? (size_t)rem : kBufSize;
      const size_t requested = size;
      const HRESULT res = Decoder.Read(buf, size);
      if (res != S_OK && res != S_FALSE)
        return res;
      RINOK(AddProcessed(size))
      if (res == S_FALSE || size != requested)
      {
        opRes = (res == S_FALSE) ? GetDataErrorResult(chunk) : (Int32)NExtract::NOperationResult::kUnexpectedEnd;
        SetFailed(chunk, Decoder.GetPos(), opRes);
        Decoder.Close();
        return S_OK;
      }
    }

    stream::inno_exe_decoder_4108 decoder4108;
    stream::inno_exe_decoder_5200 decoder5200(file.filter == stream::InstructionFilter5309);
    UInt64 rem = file.size;
    size_t held = 0;
    while (rem != 0 || held != 0)
    {
      size_t size = kBufSize - held;
      if (size > rem)
        size = (size_t)rem;
      const size_t requested = size;
      const HRESULT res = Decoder.Read(buf + held, size);
      if (res != S_OK && res != S_FALSE)
        return res;
      if (res == S_FALSE || size != requested)
      {
        opRes = (res == S_FALSE) ? GetDataErrorResult(chunk) : (Int32)NExtract::NOperationResult::kUnexpectedEnd;
        SetFailed(chunk, Decoder.GetPos(), opRes);
        Decoder.Close();
        if (outStream && size != 0)
          RINOK(WriteStream(outStream, buf, held + size))
        return S_OK;
      }
      rem -= size;
      const size_t avail = held + size;
      size_t done = avail;
      switch (file.filter)
      {
        case stream::InstructionFilter4108:
          decoder4108.decode(buf, avail);
          break;
        case stream::InstructionFilter5200:
        case stream::InstructionFilter5309:
          done = decoder5200.decode(buf, avail, rem == 0);
          break;
        default:
          break;
      }
      hasher.update((const char *)buf, done);
      if (cache)
        cache->append((const char *)buf, done);
      if (outStream)
        RINOK(WriteStream(outStream, buf, done))
      RINOK(AddProcessed(size))
      held = avail - done;
      if (held != 0)
        memmove(buf, buf + done, held);
    }
  }
  if (hasher.finalize() != file.checksum)
    opRes = NExtract::NOperationResult::kCRCError;
  return S_OK;
}

}

HRESULT CHandler::Extract2(const UInt32 *indices, UInt32 numItems, bool testMode,
    IArchiveExtractCallback *extractCallback)
{
  const bool allFilesMode = (numItems == (UInt32)(Int32)-1);
  if (allFilesMode)
    numItems = (UInt32)_arc.Items.size();
  if (numItems == 0)
    return S_OK;

  std::vector<CRequest> files;
  std::vector<CRequest> extras;
  for (UInt32 i = 0; i < numItems; i++)
  {
    const UInt32 index = allFilesMode ? i : indices[i];
    if (index >= _arc.Items.size())
      return E_INVALIDARG;
    const CItem &item = _arc.Items[index];
    CRequest r;
    r.Index = index;
    r.Planned = item.Size;
    if (item.FileIndex < 0)
    {
      r.Location = 0;
      extras.push_back(r);
    }
    else
    {
      r.Location = _arc.Info.files[(size_t)item.FileIndex].location;
      files.push_back(r);
    }
  }

  // Files are extracted in the order of their data in the installer.
  const std::vector<setup::data_entry> &entries = _arc.Info.data_entries;
  std::stable_sort(files.begin(), files.end(),
      [&entries](const CRequest &a, const CRequest &b)
      {
        const setup::data_entry &ea = entries[a.Location];
        const setup::data_entry &eb = entries[b.Location];
        if (ea.chunk.first_slice != eb.chunk.first_slice)
          return ea.chunk.first_slice < eb.chunk.first_slice;
        if (ea.chunk.offset != eb.chunk.offset)
          return ea.chunk.offset < eb.chunk.offset;
        if (ea.file.offset != eb.file.offset)
          return ea.file.offset < eb.file.offset;
        return a.Location < b.Location;
      });

  // The first item of a file also counts the data skipped before it.
  UInt64 total = 0;
  {
    const stream::chunk *prevChunk = NULL;
    UInt64 prevEnd = 0;
    for (size_t i = 0; i < files.size(); i++)
    {
      if (i != 0 && files[i].Location == files[i - 1].Location)
      {
        total += files[i].Planned;
        continue;
      }
      const setup::data_entry &entry = entries[files[i].Location];
      UInt64 gap = entry.file.offset;
      if (prevChunk && *prevChunk == entry.chunk && prevEnd <= entry.file.offset)
        gap = entry.file.offset - prevEnd;
      files[i].Planned += gap;
      total += files[i].Planned;
      prevChunk = &entry.chunk;
      prevEnd = entry.file.offset + entry.file.size;
    }
    for (size_t i = 0; i < extras.size(); i++)
      total += extras[i].Planned;
  }
  RINOK(extractCallback->SetTotal(total))

  CExtractor extractor(_arc, extractCallback);
  RINOK(extractor.SetProgress())
  const Int32 askMode = testMode ?
      NExtract::NAskMode::kTest :
      NExtract::NAskMode::kExtract;

  for (size_t i = 0; i < files.size();)
  {
    size_t end = i + 1;
    while (end < files.size() && files[end].Location == files[i].Location)
      end++;
    const setup::data_entry &entry = entries[files[i].Location];
    const Int32 staticRes = extractor.GetStaticResult(entry);
    bool haveResult = false;
    bool cacheValid = false;
    Int32 result = NExtract::NOperationResult::kOK;
    std::string cache;
    for (; i < end; i++)
    {
      extractor.BeginUnit(files[i].Planned);
      CMyComPtr<ISequentialOutStream> outStream;
      RINOK(extractCallback->GetStream(files[i].Index, &outStream, askMode))
      if (!testMode && !outStream)
      {
        RINOK(extractor.EndUnit())
        continue;
      }
      RINOK(extractCallback->PrepareOperation(askMode))
      Int32 opRes;
      if (staticRes >= 0)
        opRes = staticRes;
      else if (haveResult && (testMode || cacheValid))
      {
        if (!testMode)
          RINOK(extractor.WriteData(outStream, cache))
        opRes = result;
      }
      else
      {
        const bool useCache = !testMode && !haveResult && i + 1 < end
            && entry.file.size <= kMaxCacheSize;
        RINOK(extractor.Decode(entry, outStream, useCache ? &cache : NULL, opRes))
        if (!haveResult)
        {
          haveResult = true;
          result = opRes;
          cacheValid = useCache;
        }
      }
      outStream.Release();
      RINOK(extractCallback->SetOperationResult(opRes))
      RINOK(extractor.EndUnit())
    }
  }

  for (size_t i = 0; i < extras.size(); i++)
  {
    extractor.BeginUnit(extras[i].Planned);
    CMyComPtr<ISequentialOutStream> outStream;
    RINOK(extractCallback->GetStream(extras[i].Index, &outStream, askMode))
    if (!testMode && !outStream)
    {
      RINOK(extractor.EndUnit())
      continue;
    }
    RINOK(extractCallback->PrepareOperation(askMode))
    const CExtraFile &extra = _arc.Extras[_arc.Items[extras[i].Index].ExtraIndex];
    RINOK(extractor.WriteData(outStream, extra.GetData()))
    outStream.Release();
    RINOK(extractCallback->SetOperationResult(NExtract::NOperationResult::kOK))
    RINOK(extractor.EndUnit())
  }
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback))
{
  try
  {
    return Extract2(indices, numItems, testMode != 0, extractCallback);
  }
  catch (const std::bad_alloc &)
  {
    return E_OUTOFMEMORY;
  }
  catch (...)
  {
    return E_FAIL;
  }
}

API_FUNC_IsArc IsArc_Inno(const Byte *p, size_t size)
{
  return IsSetupLoader(p, size);
}

}}
