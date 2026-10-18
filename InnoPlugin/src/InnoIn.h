// InnoIn.h

#ifndef ZIP7_INC_ARCHIVE_INNO_IN_H
#define ZIP7_INC_ARCHIVE_INNO_IN_H

#include "CPP/Common/MyCom.h"
#include "CPP/Common/MyString.h"
#include "CPP/7zip/IStream.h"

#include "inno/loader/offsets.hpp"
#include "inno/setup/data.hpp"
#include "inno/setup/file.hpp"
#include "inno/setup/info.hpp"

#include <string>
#include <vector>

namespace NArchive {
namespace NInno {

// A file reconstructed from the setup headers, listed under "embedded\".
struct CExtraFile
{
  std::string Name;          // UTF-8 file name
  std::string Content;       // generated content
  const std::string *Stored; // content stored in the setup headers, or NULL

  const std::string &GetData() const { return Stored ? *Stored : Content; }
};

struct CItem
{
  UString Path;        // unique path with the path separators of the host
  UInt64 Size;
  int FileIndex;       // index in Info.files, or -1 for extra files
  unsigned ExtraIndex; // index in Extras for extra files
};

class CInArchive
{
  void AddExtraFile(const std::string &name, const std::string *stored);
  void BuildExtraFiles();
  void BuildItems();
  void ComputeSizes(const Byte *peHeaders, size_t peHeadersSize);

public:
  CMyComPtr<IInStream> Stream;
  UInt64 FileSize;
  UInt64 PhySize;
  loader::offsets Offsets;
  setup::info Info;
  std::vector<CItem> Items;
  std::vector<CExtraFile> Extras;
  std::vector<std::string> Warnings;
  std::vector<bool> SolidEntries; // per data entry: its chunk holds other data entries
  bool IsSolid;
  AString Method;
  UString Comment;

  CInArchive(): FileSize(0), PhySize(0), IsSolid(false) {}

  // Returns S_FALSE if the stream is not an Inno Setup installer that can be
  // opened, or the error of the stream if reading it failed.
  HRESULT Open(IInStream *stream);
  void Clear();

  const setup::data_entry &GetDataEntry(const CItem &item) const
    { return Info.data_entries[Info.files[(size_t)item.FileIndex].location]; }
  // Position of the end of the stored chunk in the installer.
  UInt64 GetChunkEnd(const stream::chunk &chunk) const;
};

// Compression method, filter and encryption of a data entry, e.g. "LZMA2 IF5309".
AString GetMethodString(const setup::data_entry &entry);

void Utf8ToUnicode(const std::string &src, UString &dest);

}}

#endif
