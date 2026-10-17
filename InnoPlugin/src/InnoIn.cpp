// InnoIn.cpp

#include "StdAfx.h"

#include "CPP/Common/UTFConvert.h"
#include "CPP/7zip/Archive/IArchive.h"
#include "CPP/7zip/Common/StreamUtils.h"

#include "InnoIn.h"
#include "InnoInterop.h"
#include "InnoPe.h"
#include "InnoScript.h"

#include "inno/setup/component.hpp"
#include "inno/setup/delete.hpp"
#include "inno/setup/directory.hpp"
#include "inno/setup/icon.hpp"
#include "inno/setup/ini.hpp"
#include "inno/setup/language.hpp"
#include "inno/setup/message.hpp"
#include "inno/setup/permission.hpp"
#include "inno/setup/registry.hpp"
#include "inno/setup/run.hpp"
#include "inno/setup/task.hpp"
#include "inno/setup/type.hpp"
#include "inno/util/log.hpp"

#include <istream>
#include <map>
#include <set>
#include <sstream>

namespace NArchive {
namespace NInno {

static const size_t kMaxWarnings = 16;
static const size_t kPeHeadersSize = (size_t)1 << 16;
static const unsigned kChunkMagicSize = 4;
static const unsigned kChunkSaltSize = 8;

void Utf8ToUnicode(const std::string &src, UString &dest)
{
  Convert_UTF8_Buf_To_Unicode(src.data(), src.size(), dest);
}

static const char * const kCompressionNames[] =
  { "Copy", "Deflate", "BZip2", "LZMA", "LZMA2", "Unknown" };
static const char * const kFilterNames[] =
  { NULL, "IF4108", "IF5200", "IF5309", NULL };
static const char * const kEncryptionName = "ARC4";

static unsigned GetCompressionIndex(const stream::chunk &chunk)
{
  const unsigned i = (unsigned)chunk.compression;
  return i < Z7_ARRAY_SIZE(kCompressionNames) ? i : Z7_ARRAY_SIZE(kCompressionNames) - 1;
}

static const char *GetFilterName(const stream::file &file)
{
  const unsigned i = (unsigned)file.filter;
  return i < Z7_ARRAY_SIZE(kFilterNames) ? kFilterNames[i] : NULL;
}

AString GetMethodString(const setup::data_entry &entry)
{
  AString s (kCompressionNames[GetCompressionIndex(entry.chunk)]);
  const char *filter = GetFilterName(entry.file);
  if (filter)
  {
    s.Add_Space();
    s += filter;
  }
  if (entry.chunk.encryption != stream::Plaintext)
  {
    s.Add_Space();
    s += kEncryptionName;
  }
  return s;
}

static std::string ToLowerAscii(const std::string &s)
{
  std::string r(s);
  for (size_t i = 0; i < r.size(); i++)
    if (r[i] >= 'A' && r[i] <= 'Z')
      r[i] = (char)(r[i] - 'A' + 'a');
  return r;
}

// A component or language expression that is a single name.
static bool IsSimpleName(const std::string &s)
{
  if (s.empty())
    return false;
  for (size_t i = 0; i < s.size(); i++)
  {
    const char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')
      continue;
    if (i != 0 && c >= '0' && c <= '9')
      continue;
    return false;
  }
  return true;
}

// Inserts suffix before the extension of the file name in path.
static std::string InsertSuffix(const std::string &path, const std::string &suffix)
{
  if (suffix.empty())
    return path;
  const size_t sep = path.rfind('\\');
  const size_t nameStart = (sep == std::string::npos) ? 0 : sep + 1;
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos || dot <= nameStart)
    return path + suffix;
  return path.substr(0, dot) + suffix + path.substr(dot);
}

static std::string GetArchSuffix(const setup::file_entry &file)
{
  const bool bits32 = (file.options & setup::file_entry::Bits32) != 0;
  const bool bits64 = (file.options & setup::file_entry::Bits64) != 0;
  if (bits32 && !bits64)
    return "@32bit";
  if (bits64 && !bits32)
    return "@64bit";
  return std::string();
}

void CInArchive::Clear()
{
  Stream.Release();
  FileSize = 0;
  PhySize = 0;
  Offsets = loader::offsets();
  Info = setup::info();
  Items.clear();
  Extras.clear();
  Warnings.clear();
  SolidEntries.clear();
  IsSolid = false;
  Method.Empty();
  Comment.Empty();
}

UInt64 CInArchive::GetChunkEnd(const stream::chunk &chunk) const
{
  return (UInt64)Offsets.data_offset + chunk.offset + kChunkMagicSize
      + (chunk.encryption != stream::Plaintext ? kChunkSaltSize : 0) + chunk.size;
}

void CInArchive::AddExtraFile(const std::string &name, const std::string *stored)
{
  CExtraFile extra;
  extra.Name = name;
  extra.Stored = stored;
  Extras.push_back(extra);
}

static const char *GetTextExtension(const std::string &text)
{
  return (text.compare(0, 5, "{\\rtf") == 0) ? ".rtf" : ".txt";
}

void CInArchive::BuildExtraFiles()
{
  AddExtraFile("install_script.iss", NULL);
  GenerateInstallScript(Info, Extras.back().Content);
  const std::string &license = Info.header.license_text;
  if (!license.empty())
    AddExtraFile(std::string("License") + GetTextExtension(license), &license);
  for (size_t i = 0; i < Info.languages.size(); i++)
  {
    AddExtraFile(Info.languages[i].name + ".isl", NULL);
    GenerateLanguageFile(Info.languages[i], Extras.back().Content);
  }
  for (size_t i = 0; i < Info.languages.size(); i++)
  {
    const setup::language_entry &lang = Info.languages[i];
    if (!lang.license_text.empty())
      AddExtraFile(lang.name + GetTextExtension(lang.license_text), &lang.license_text);
  }
  for (size_t i = 0; i < Info.wizard_images.size(); i++)
  {
    std::ostringstream name;
    name << "WizardImage" << i << ".bmp";
    AddExtraFile(name.str(), &Info.wizard_images[i]);
  }
  for (size_t i = 0; i < Info.wizard_images_small.size(); i++)
  {
    std::ostringstream name;
    name << "WizardImageSmall" << i << ".bmp";
    AddExtraFile(name.str(), &Info.wizard_images_small[i]);
  }
}

/*
Item names: files that have data in the installer keep their destination path
("{app}\file.txt"), extra files are listed under "embedded\". Files with the
same destination (ignoring case) get a suffix before the extension that
describes how they differ: "#component", "@language" and "@32bit" / "@64bit"
for simple conditions that are not the same for all of them. Then every name
that is still taken gets "$1", "$2", ... in the order of the items.
*/

void CInArchive::BuildItems()
{
  std::vector<size_t> files;
  for (size_t i = 0; i < Info.files.size(); i++)
    if (Info.files[i].location < Info.data_entries.size())
      files.push_back(i);

  std::vector<std::string> names(files.size());
  std::map<std::string, std::vector<size_t> > groups;
  for (size_t k = 0; k < files.size(); k++)
  {
    names[k] = Info.files[files[k]].destination;
    groups[ToLowerAscii(names[k])].push_back(k);
  }

  for (std::map<std::string, std::vector<size_t> >::const_iterator it = groups.begin();
      it != groups.end(); ++it)
  {
    const std::vector<size_t> &group = it->second;
    if (group.size() < 2)
      continue;
    const setup::file_entry &first = Info.files[files[group[0]]];
    bool sameComponents = true;
    bool sameLanguages = true;
    bool sameArch = true;
    for (size_t j = 1; j < group.size(); j++)
    {
      const setup::file_entry &file = Info.files[files[group[j]]];
      sameComponents = sameComponents && (file.components == first.components);
      sameLanguages = sameLanguages && (file.languages == first.languages);
      sameArch = sameArch && (GetArchSuffix(file) == GetArchSuffix(first));
    }
    for (size_t j = 0; j < group.size(); j++)
    {
      const setup::file_entry &file = Info.files[files[group[j]]];
      std::string suffix;
      if (!sameComponents && IsSimpleName(file.components))
        suffix += "#" + file.components;
      if (!sameLanguages && IsSimpleName(file.languages))
        suffix += "@" + file.languages;
      if (!sameArch)
        suffix += GetArchSuffix(file);
      names[group[j]] = InsertSuffix(names[group[j]], suffix);
    }
  }

  std::set<std::string> used;
  Items.reserve(files.size() + Extras.size());
  for (size_t k = 0; k < files.size() + Extras.size(); k++)
  {
    CItem item;
    std::string name;
    if (k < files.size())
    {
      const setup::file_entry &file = Info.files[files[k]];
      name = names[k];
      item.FileIndex = (int)files[k];
      item.ExtraIndex = 0;
      item.Size = Info.data_entries[file.location].file.size;
    }
    else
    {
      const CExtraFile &extra = Extras[k - files.size()];
      name = "embedded\\" + extra.Name;
      item.FileIndex = -1;
      item.ExtraIndex = (unsigned)(k - files.size());
      item.Size = extra.GetData().size();
    }
    if (!used.insert(ToLowerAscii(name)).second)
    {
      for (unsigned n = 1;; n++)
      {
        std::ostringstream suffix;
        suffix << '$' << n;
        const std::string candidate = InsertSuffix(name, suffix.str());
        if (used.insert(ToLowerAscii(candidate)).second)
        {
          name = candidate;
          break;
        }
      }
    }
    Utf8ToUnicode(name, item.Path);
    #if WCHAR_PATH_SEPARATOR != L'\\'
    item.Path.Replace(L'\\', WCHAR_PATH_SEPARATOR);
    #endif
    Items.push_back(item);
  }
}

void CInArchive::ComputeSizes(const Byte *peHeaders, size_t peHeadersSize)
{
  // Data entries in the same chunk are solid.
  std::map<stream::chunk, size_t> chunkEntries;
  for (size_t i = 0; i < Info.data_entries.size(); i++)
    chunkEntries[Info.data_entries[i].chunk]++;
  SolidEntries.resize(Info.data_entries.size());
  for (size_t i = 0; i < Info.data_entries.size(); i++)
  {
    SolidEntries[i] = (chunkEntries[Info.data_entries[i].chunk] > 1);
    if (SolidEntries[i])
      IsSolid = true;
  }

  // The archive method lists the methods, filters and encryption of all files.
  bool compressions[Z7_ARRAY_SIZE(kCompressionNames)] = { false };
  bool filters[Z7_ARRAY_SIZE(kFilterNames)] = { false };
  bool encrypted = false;
  for (size_t i = 0; i < Items.size(); i++)
  {
    if (Items[i].FileIndex < 0)
      continue;
    const setup::data_entry &entry = GetDataEntry(Items[i]);
    compressions[GetCompressionIndex(entry.chunk)] = true;
    if (GetFilterName(entry.file))
      filters[(unsigned)entry.file.filter] = true;
    if (entry.chunk.encryption != stream::Plaintext)
      encrypted = true;
  }
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kCompressionNames); i++)
    if (compressions[i])
    {
      Method.Add_Space_if_NotEmpty();
      Method += kCompressionNames[i];
    }
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kFilterNames); i++)
    if (filters[i])
    {
      Method.Add_Space_if_NotEmpty();
      Method += kFilterNames[i];
    }
  if (encrypted)
  {
    Method.Add_Space_if_NotEmpty();
    Method += kEncryptionName;
  }

  // The compiler writes the size of the installer to the offset table.
  // Signing appends a certificate.
  UInt64 phySize = Offsets.total_size;
  if (Offsets.data_offset != 0)
    for (size_t i = 0; i < Info.data_entries.size(); i++)
    {
      const UInt64 end = GetChunkEnd(Info.data_entries[i].chunk);
      if (phySize < end)
        phySize = end;
    }
  CPeHeaders pe;
  if (ParsePeHeaders(peHeaders, peHeadersSize, pe) == k_IsArc_Res_YES && pe.CertSize != 0)
  {
    const UInt64 end = (UInt64)pe.CertPos + pe.CertSize;
    if (phySize < end)
      phySize = end;
  }
  PhySize = phySize;
}

HRESULT CInArchive::Open(IInStream *stream)
{
  Clear();
  RINOK(InStream_GetSize_SeekToEnd(stream, FileSize))
  RINOK(InStream_SeekToBegin(stream))
  size_t peSize = kPeHeadersSize;
  if (peSize > FileSize)
    peSize = (size_t)FileSize;
  CByteBuffer peHeaders(peSize);
  RINOK(ReadStream(stream, peHeaders, &peSize))
  if (peSize < 2 || peHeaders[0] != 'M' || peHeaders[1] != 'Z')
    return S_FALSE;

  bool ok = false;
  log_capture capture;
  try
  {
    CInStreamBuf streamBuf(stream);
    std::istream is(&streamBuf);
    Offsets.load(is);
    if (Offsets.found_magic)
    {
      is.clear();
      is.seekg(Offsets.header_offset);
      Info.load(is, setup::info::entry_types::all());
      ok = true;
    }
    else
      debug("no setup loader found");
  }
  catch (const std::exception &e)
  {
    log_error << "cannot parse the setup headers: " << e.what();
  }
  capture.restore();
  if (ok)
  {
    size_t numWarnings = 0;
    std::set<std::string> seen;
    const std::vector<log_capture::message> &messages = capture.get();
    for (size_t i = 0; i < messages.size(); i++)
    {
      if (messages[i].level < logger::Warning || !seen.insert(messages[i].text).second)
        continue;
      if (numWarnings++ < kMaxWarnings)
        Warnings.push_back(messages[i].text);
    }
    if (numWarnings > kMaxWarnings)
    {
      std::ostringstream oss;
      oss << (numWarnings - kMaxWarnings) << " more warnings";
      Warnings.push_back(oss.str());
    }
  }
  capture.flush();
  if (!ok)
  {
    Clear();
    return S_FALSE;
  }

  Stream = stream;
  BuildExtraFiles();
  BuildItems();
  ComputeSizes(peHeaders, peSize);

  std::ostringstream comment;
  comment << "InnoSetupVersion = " << Info.version << '\n';
  if (!Info.header.app_name.empty())
    comment << "AppName = " << Info.header.app_name << '\n';
  if (!Info.header.app_version.empty())
    comment << "AppVersion = " << Info.header.app_version << '\n';
  Utf8ToUnicode(comment.str(), Comment);
  return S_OK;
}

}}
