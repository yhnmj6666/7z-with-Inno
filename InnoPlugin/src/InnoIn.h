// InnoIn.h

#ifndef ZIP7_INC_ARCHIVE_INNO_IN_H
#define ZIP7_INC_ARCHIVE_INNO_IN_H

#include "CPP/Common/MyCom.h"
#include "CPP/Common/MyString.h"
#include "CPP/7zip/IStream.h"
#include "CPP/7zip/Archive/IArchive.h"

#include "InnoDecoder.h"

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
  void SetError(UInt32 flags, const std::string &message);
  HRESULT OpenSlices(IArchiveOpenCallback *callback);
  // Loads the setup headers after Info.load_version, with the password for
  // Encryption=full. Returns the error of the callback.
  HRESULT LoadHeaders(std::istream &is, IArchiveOpenCallback *callback);

public:
  CMyComPtr<IInStream> Stream;
  UInt64 FileSize;
  UInt64 PhySize;
  loader::offsets Offsets;
  // The setup data version of the installer. For a version newer than all
  // known ones, Info.version is the newest known version, whose format is
  // used to read it.
  setup::version Version;
  setup::info Info;
  std::vector<CItem> Items;
  std::vector<CExtraFile> Extras;
  std::vector<std::string> Warnings;
  std::vector<bool> SolidEntries; // per data entry: its chunk holds other data entries
  bool IsSolid;
  AString Method;
  UString Comment;
  // An installer that cannot be listed (unsupported version, encrypted
  // headers without the right password) opens without items and reports
  // the error through kpidErrorFlags and kpidError.
  UInt32 ErrorFlags;
  std::string ErrorMessage;
  // Checked key of the password (setup::info::get_key).
  std::string Key;
  bool KeyDefined;
  // The slice files of a disk-spanning installer (Offsets.data_offset == 0),
  // opened through IArchiveOpenVolumeCallback.
  std::vector<CSlice> Slices;

  CInArchive(): FileSize(0), PhySize(0), IsSolid(false), ErrorFlags(0), KeyDefined(false) {}

  // Returns S_FALSE if the stream is not an Inno Setup installer that can be
  // opened, or the error of the stream or the callback if reading it or
  // getting the password failed.
  HRESULT Open(IInStream *stream, IArchiveOpenCallback *callback);
  void Clear();

  // Gets the password from the ICryptoGetTextPassword interface of callback
  // and sets Key if it is right. Returns the error of the callback.
  HRESULT GetPassword(IUnknown *callback);
  // Sets Key if password is the password of the installer.
  bool SetPassword(const UString &password);
  // PBKDF2 iteration count of the password (0 if there is none).
  UInt32 GetKdfIterations() const;
  // False if the installer asks for more PBKDF2 iterations than the plugin
  // does for one password.
  bool IsKdfSupported() const;

  const setup::data_entry &GetDataEntry(const CItem &item) const
    { return Info.data_entries[Info.files[(size_t)item.FileIndex].location]; }
  // Position of the end of the stored chunk in the installer.
  UInt64 GetChunkEnd(const stream::chunk &chunk) const;
  bool IsSpanned() const { return Offsets.data_offset == 0; }
  // False if the slice with the start of the chunk is missing. A later
  // missing slice is found while reading (CSliceInStream::MissingSlice):
  // the files of a solid chunk before it can still be extracted.
  bool IsChunkStartAvailable(const stream::chunk &chunk) const;
  // Returns a stream at the start of the chunk. sliceStream is set for
  // disk-spanning installers.
  HRESULT OpenChunkStream(const stream::chunk &chunk, CMyComPtr<ISequentialInStream> &stream,
      CSliceInStream **sliceStream) const;
};

// Compression method, filter and encryption of a data entry, e.g. "LZMA2 IF5309".
AString GetMethodString(const setup::data_entry &entry);

void Utf8ToUnicode(const std::string &src, UString &dest);

}}

#endif
