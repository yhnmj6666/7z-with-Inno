// InnoIn.cpp

#include "StdAfx.h"

#include <string.h>

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
("{app}\file.txt"), extra files are listed under "embedded\". File names are
never changed. Files whose destinations are the same ignoring case get leading
folders, one for each property that differs within the group, in this order:
  "{x86}", "{x64}" or "{arm64}": the architecture of the Check parameter, or
      of the 32bit / 64bit flags if the Check parameter does not select one,
  "{#component}": a Components parameter that is a single name,
  "{@language}": a Languages parameter that is a single name.
Then items whose paths are still taken get "{$1}", "{$2}", ... in item order.
*/

static const unsigned kArch_x86 = 1 << 0;
static const unsigned kArch_x64 = 1 << 1;
static const unsigned kArch_arm64 = 1 << 2;
static const unsigned kArch_All = kArch_x86 | kArch_x64 | kArch_arm64;

static const struct
{
  const char *Name;
  unsigned Archs; // architectures where the function returns true
} kArchFunctions[] =
{
  { "Is64BitInstallMode", kArch_x64 | kArch_arm64 },
  { "IsWin64", kArch_x64 | kArch_arm64 },
  { "IsX64Compatible", kArch_x64 | kArch_arm64 },
  { "IsX64", kArch_x64 },
  { "IsX64OS", kArch_x64 },
  { "IsARM64", kArch_arm64 },
  { "IsX86", kArch_x86 },
  { "IsX86OS", kArch_x86 },
  { "IsX86Compatible", kArch_All }
};

static bool IsIdentStart(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsIdentChar(char c)
{
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

// Evaluates a Check expression of the functions in kArchFunctions, "and",
// "or", "not" and parentheses for all architectures at once: each value is
// the set of architectures where it is true.
class CCheckEvaluator
{
  const char *_cur;
  const char *_end;
  std::string _token; // identifier, "(" or ")"; empty at the end
  bool _error;

  void Next();
  bool IsToken(const char *s) const { return StringsAreEqualNoCase_Ascii(_token.c_str(), s); }
  unsigned ParseOr(unsigned depth);
  unsigned ParseAnd(unsigned depth);
  unsigned ParseFactor(unsigned depth);
  unsigned SetError() { _error = true; return 0; }

public:
  // Returns false if the expression uses anything else.
  bool Evaluate(const std::string &expr, unsigned &archs);
};

void CCheckEvaluator::Next()
{
  while (_cur != _end && (*_cur == ' ' || *_cur == '\t' || *_cur == '\r' || *_cur == '\n'))
    _cur++;
  _token.clear();
  if (_cur == _end)
    return;
  const char *start = _cur;
  if (*_cur == '(' || *_cur == ')')
    _cur++;
  else if (IsIdentStart(*_cur))
  {
    while (_cur != _end && IsIdentChar(*_cur))
      _cur++;
  }
  else
  {
    _error = true;
    _cur = _end;
    return;
  }
  _token.assign(start, _cur);
}

unsigned CCheckEvaluator::ParseOr(unsigned depth)
{
  unsigned archs = ParseAnd(depth);
  while (!_error && IsToken("or"))
  {
    Next();
    archs |= ParseAnd(depth);
  }
  return archs;
}

unsigned CCheckEvaluator::ParseAnd(unsigned depth)
{
  unsigned archs = ParseFactor(depth);
  while (!_error && IsToken("and"))
  {
    Next();
    archs &= ParseFactor(depth);
  }
  return archs;
}

unsigned CCheckEvaluator::ParseFactor(unsigned depth)
{
  const unsigned kMaxDepth = 64;
  if (_error || depth >= kMaxDepth)
    return SetError();
  if (IsToken("("))
  {
    Next();
    const unsigned archs = ParseOr(depth + 1);
    if (_error || !IsToken(")"))
      return SetError();
    Next();
    return archs;
  }
  if (IsToken("not"))
  {
    Next();
    return ~ParseFactor(depth + 1) & kArch_All;
  }
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kArchFunctions); i++)
    if (IsToken(kArchFunctions[i].Name))
    {
      Next();
      if (IsToken("("))
      {
        // Only a call without arguments.
        Next();
        if (!IsToken(")"))
          return SetError();
        Next();
      }
      return kArchFunctions[i].Archs;
    }
  return SetError();
}

bool CCheckEvaluator::Evaluate(const std::string &expr, unsigned &archs)
{
  _cur = expr.c_str();
  _end = _cur + expr.size();
  _error = false;
  Next();
  archs = ParseOr(0);
  return !_error && _token.empty();
}

static const char *GetArchName(const setup::file_entry &file)
{
  unsigned archs = 0;
  CCheckEvaluator evaluator;
  if (evaluator.Evaluate(file.check, archs))
  {
    if (archs == kArch_x86)
      return "x86";
    if (archs == kArch_x64 || archs == (kArch_x64 | kArch_arm64))
      return "x64";
    if (archs == kArch_arm64)
      return "arm64";
  }
  const bool bits32 = (file.options & setup::file_entry::Bits32) != 0;
  const bool bits64 = (file.options & setup::file_entry::Bits64) != 0;
  if (bits64 && !bits32)
    return "x64";
  if (bits32 && !bits64)
    return "x86";
  return NULL;
}

// Returns the name if expr is a single component or language name, as Inno
// Setup checks them: letters, digits and "_", not starting with a digit.
// Component names can contain "\" for subcomponents.
static std::string GetSingleName(const std::string &expr, bool isComponent)
{
  const size_t start = expr.find_first_not_of(" \t");
  if (start == std::string::npos)
    return std::string();
  const size_t end = expr.find_last_not_of(" \t") + 1;
  const std::string name = expr.substr(start, end - start);
  if (StringsAreEqualNoCase_Ascii(name.c_str(), "not")
      || StringsAreEqualNoCase_Ascii(name.c_str(), "and")
      || StringsAreEqualNoCase_Ascii(name.c_str(), "or"))
    return std::string();
  for (size_t i = 0; i < name.size(); i++)
  {
    const char c = name[i];
    if (IsIdentStart(c) || (i != 0 && c >= '0' && c <= '9'))
      continue;
    if (isComponent && i != 0 && i + 1 != name.size() && (c == '\\' || c == '/'))
      continue;
    return std::string();
  }
  return name;
}

// Replaces characters that Windows does not allow in file names.
static void SanitizeLabel(std::string &s)
{
  for (size_t i = 0; i < s.size(); i++)
  {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || strchr("<>:\"/\\|?*", c))
      s[i] = '_';
  }
}

// Paths that differ only in case are the same file on Windows. Other hosts
// ignore the case of ASCII letters only, so that the names do not depend on
// the locale.
static std::wstring GetPathKey(const UString &path)
{
  std::wstring key;
  key.reserve(path.Len());
  for (unsigned i = 0; i < path.Len(); i++)
  {
    wchar_t c = path[i];
    #ifdef _WIN32
    c = MyCharUpper(c);
    #else
    if (c >= 'a' && c <= 'z')
      c = (wchar_t)(c - 0x20);
    #endif
    key += c;
  }
  return key;
}

void CInArchive::BuildItems()
{
  std::vector<size_t> files;
  for (size_t i = 0; i < Info.files.size(); i++)
    if (Info.files[i].location < Info.data_entries.size())
      files.push_back(i);
  const size_t numItems = files.size() + Extras.size();

  std::vector<UString> dests(numItems);
  for (size_t k = 0; k < numItems; k++)
  {
    if (k < files.size())
      Utf8ToUnicode(Info.files[files[k]].destination, dests[k]);
    else
      Utf8ToUnicode("embedded\\" + Extras[k - files.size()].Name, dests[k]);
  }

  std::vector<std::string> folders(numItems);
  std::map<std::wstring, std::vector<size_t> > groups;
  for (size_t k = 0; k < files.size(); k++)
    groups[GetPathKey(dests[k])].push_back(k);
  for (std::map<std::wstring, std::vector<size_t> >::const_iterator it = groups.begin();
      it != groups.end(); ++it)
  {
    const std::vector<size_t> &group = it->second;
    if (group.size() < 2)
      continue;
    const unsigned kNumProps = 3;
    static const char * const kPrefixes[kNumProps] = { "{", "{#", "{@" };
    std::vector<std::string> labels[kNumProps];
    for (size_t j = 0; j < group.size(); j++)
    {
      const setup::file_entry &file = Info.files[files[group[j]]];
      const char *arch = GetArchName(file);
      labels[0].push_back(arch ? arch : "");
      labels[1].push_back(GetSingleName(file.components, true));
      labels[2].push_back(GetSingleName(file.languages, false));
    }
    for (unsigned p = 0; p < kNumProps; p++)
    {
      bool differ = false;
      for (size_t j = 1; j < group.size(); j++)
        if (!StringsAreEqualNoCase_Ascii(labels[p][j].c_str(), labels[p][0].c_str()))
          differ = true;
      if (!differ)
        continue;
      for (size_t j = 0; j < group.size(); j++)
      {
        std::string label = labels[p][j];
        if (label.empty())
          continue;
        SanitizeLabel(label);
        folders[group[j]] += kPrefixes[p] + label + "}\\";
      }
    }
  }

  std::set<std::wstring> used;
  std::map<std::wstring, unsigned> lastNumbers;
  Items.reserve(numItems);
  for (size_t k = 0; k < numItems; k++)
  {
    CItem item;
    UString folder;
    Utf8ToUnicode(folders[k], folder);
    item.Path = folder + dests[k];
    const std::wstring key = GetPathKey(item.Path);
    if (!used.insert(key).second)
    {
      unsigned &n = lastNumbers[key];
      for (;;)
      {
        UString path = folder;
        path += "{$";
        path.Add_UInt32(++n);
        path += "}\\";
        path += dests[k];
        if (used.insert(GetPathKey(path)).second)
        {
          item.Path = path;
          break;
        }
      }
    }
    #if WCHAR_PATH_SEPARATOR != L'\\'
    item.Path.Replace(L'\\', WCHAR_PATH_SEPARATOR);
    #endif
    if (k < files.size())
    {
      item.FileIndex = (int)files[k];
      item.ExtraIndex = 0;
      item.Size = Info.data_entries[Info.files[files[k]].location].file.size;
    }
    else
    {
      item.FileIndex = -1;
      item.ExtraIndex = (unsigned)(k - files.size());
      item.Size = Extras[item.ExtraIndex].GetData().size();
    }
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
  CInStreamBuf streamBuf(stream);
  log_capture capture;
  try
  {
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
    // A read error or E_ABORT from the stream ends the data while parsing.
    const HRESULT res = streamBuf.GetError();
    return res != S_OK ? res : S_FALSE;
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
