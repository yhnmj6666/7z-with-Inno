// InnoClient.cpp
// Test client for the Inno plugin. It loads the plugin through the same exports
// 7-Zip uses (CreateObject, GetNumberOfFormats, GetHandlerProperty2, GetIsArc,
// GetModuleProp) and lists, tests or extracts an installer. The output is one
// JSON object per line, for tests/run_tests.py.
//
// Adapted from 7-Zip 26.04 CPP/7zip/UI/Client7z/Client7z.cpp (GNU LGPL, see
// THIRD-PARTY-NOTICES.md): archive creation removed; format lookup by name,
// IArchiveOpenVolumeCallback, test mode, path checks and JSON output added.
//
// Usage:
//   inno_client <plugin> i
//   inno_client <plugin> l <archive> [-p<password>]
//   inno_client <plugin> t <archive> [-p<password>] [-i<index>[,<index>...]]
//   inno_client <plugin> x <archive> <output folder> [-p<password>] [-i<index>[,<index>...]] [-s]
//
// -i passes only these items to IInArchive::Extract, in the given order.
// -s skips items whose output file exists: GetStream returns no stream, as
// 7-Zip does for -aos.
//
// Exit codes: 0 success, 1 usage or plugin error, 2 the archive cannot be opened,
// 3 IInArchive::Extract failed, 4 some items reported errors.

#include "CPP/Common/Common.h"

#include <stdio.h>
#include <string.h>

#include "CPP/Common/MyWindows.h"
#include "CPP/Common/MyInitGuid.h"

#include "CPP/Common/Defs.h"
#include "CPP/Common/IntToString.h"
#include "CPP/Common/StringConvert.h"
#include "CPP/Common/StringToInt.h"
#include "CPP/Common/UTFConvert.h"

#include "CPP/Windows/DLL.h"
#include "CPP/Windows/FileDir.h"
#include "CPP/Windows/FileFind.h"
#include "CPP/Windows/FileName.h"
#include "CPP/Windows/NtCheck.h"
#include "CPP/Windows/PropVariant.h"
#include "CPP/Windows/TimeUtils.h"

#include "CPP/7zip/Common/FileStreams.h"

#include "CPP/7zip/Archive/IArchive.h"
#include "CPP/7zip/ICoder.h"
#include "CPP/7zip/IPassword.h"

#ifdef _WIN32
extern
HINSTANCE g_hInstance;
HINSTANCE g_hInstance = NULL;
#endif

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

using namespace NWindows;
using namespace NFile;
using namespace NDir;

static const char * const kFormatName = "Inno";

static const char * const kHelpString =
  "Usage:\n"
  "  inno_client <plugin> i\n"
  "  inno_client <plugin> l <archive> [-p<password>]\n"
  "  inno_client <plugin> t <archive> [-p<password>] [-i<index>[,<index>...]]\n"
  "  inno_client <plugin> x <archive> <output folder> [-p<password>] [-i<index>[,<index>...]] [-s]\n"
  "  -i  extract or test only these items, in this order\n"
  "  -s  skip items whose output file exists\n";

enum
{
  k_Exit_OK = 0,
  k_Exit_Usage = 1,
  k_Exit_Open = 2,
  k_Exit_Extract = 3,
  k_Exit_ItemErrors = 4
};


//////////////////////////////////////////////////////////////
// JSON output

class CJsonLine
{
  AString _s;
  bool _first;

  void Key(const char *key)
  {
    if (!_first)
      _s += ',';
    _first = false;
    AddString(AString(key));
    _s += ':';
  }

  void AddString(const AString &utf8)
  {
    _s += '\"';
    for (unsigned i = 0; i < utf8.Len(); i++)
    {
      const unsigned char c = (unsigned char)utf8[i];
      if (c == '\"' || c == '\\')
      {
        _s += '\\';
        _s += (char)c;
      }
      else if (c < 0x20)
      {
        static const char * const kHex = "0123456789abcdef";
        _s += "\\u00";
        _s += kHex[c >> 4];
        _s += kHex[c & 15];
      }
      else
        _s += (char)c;
    }
    _s += '\"';
  }

public:
  CJsonLine(const char *type): _first(true)
  {
    _s = '{';
    Add("type", type);
  }

  void Add(const char *key, const char *value)
  {
    Key(key);
    AddString(AString(value));
  }

  void Add(const char *key, const AString &value)
  {
    Key(key);
    AddString(value);
  }

  void Add(const char *key, const UString &value)
  {
    AString utf8;
    ConvertUnicodeToUTF8(value, utf8);
    Key(key);
    AddString(utf8);
  }

  void AddUInt64(const char *key, UInt64 value)
  {
    char temp[32];
    ConvertUInt64ToString(value, temp);
    Key(key);
    _s += temp;
  }

  void AddInt64(const char *key, Int64 value)
  {
    char temp[32];
    ConvertInt64ToString(value, temp);
    Key(key);
    _s += temp;
  }

  void AddBool(const char *key, bool value)
  {
    Key(key);
    _s += (value ? "true" : "false");
  }

  void AddNull(const char *key)
  {
    Key(key);
    _s += "null";
  }

  void AddHex32(const char *key, UInt32 value)
  {
    char temp[16];
    temp[0] = '0';
    temp[1] = 'x';
    ConvertUInt32ToHex8Digits(value, temp + 2);
    Add(key, temp);
  }

  // Adds a property value: strings, numbers, booleans, and FILETIME as the
  // number of 100 ns intervals since 1601-01-01.
  void AddProp(const char *key, const PROPVARIANT &prop)
  {
    switch (prop.vt)
    {
      case VT_EMPTY: AddNull(key); break;
      case VT_BSTR: Add(key, UString(prop.bstrVal)); break;
      case VT_BOOL: AddBool(key, VARIANT_BOOLToBool(prop.boolVal)); break;
      case VT_UI1: AddUInt64(key, prop.bVal); break;
      case VT_UI2: AddUInt64(key, prop.uiVal); break;
      case VT_UI4: AddUInt64(key, prop.ulVal); break;
      case VT_UI8: AddUInt64(key, prop.uhVal.QuadPart); break;
      case VT_I2: AddInt64(key, prop.iVal); break;
      case VT_I4: AddInt64(key, prop.lVal); break;
      case VT_I8: AddInt64(key, prop.hVal.QuadPart); break;
      case VT_FILETIME:
        AddUInt64(key, ((UInt64)prop.filetime.dwHighDateTime << 32) | prop.filetime.dwLowDateTime);
        break;
      default:
      {
        AString s ("unsupported VARTYPE ");
        s.Add_UInt32(prop.vt);
        Add(key, s);
      }
    }
  }

  void Print()
  {
    _s += '}';
    _s.Add_LF();
    fputs(_s.Ptr(), stdout);
    fflush(stdout);
  }
};

static void PrintError(const char *message, const FString &name = FString(), HRESULT res = S_OK)
{
  CJsonLine j("error");
  j.Add("message", message);
  if (!name.IsEmpty())
    j.Add("name", fs2us(name));
  if (res != S_OK)
    j.AddHex32("hresult", (UInt32)res);
  j.Print();
}

static AString GetPropName(PROPID propID)
{
  switch (propID)
  {
    case kpidPath: return AString("path");
    case kpidName: return AString("name");
    case kpidIsDir: return AString("is_dir");
    case kpidSize: return AString("size");
    case kpidPackSize: return AString("pack_size");
    case kpidAttrib: return AString("attrib");
    case kpidCTime: return AString("ctime");
    case kpidATime: return AString("atime");
    case kpidMTime: return AString("mtime");
    case kpidSolid: return AString("solid");
    case kpidCommented: return AString("commented");
    case kpidEncrypted: return AString("encrypted");
    case kpidCRC: return AString("crc");
    case kpidMethod: return AString("method");
    case kpidComment: return AString("comment");
    case kpidOffset: return AString("offset");
    case kpidHeadersSize: return AString("headers_size");
    case kpidPhySize: return AString("phy_size");
    case kpidWarning: return AString("warning");
    case kpidError: return AString("error");
    case kpidErrorFlags: return AString("error_flags");
    case kpidWarningFlags: return AString("warning_flags");
    default:
    {
      AString s ("prop_");
      s.Add_UInt32(propID);
      return s;
    }
  }
}

static const char *GetOperationResultName(Int32 opRes)
{
  switch (opRes)
  {
    case NArchive::NExtract::NOperationResult::kOK: return "ok";
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod: return "unsupported_method";
    case NArchive::NExtract::NOperationResult::kDataError: return "data_error";
    case NArchive::NExtract::NOperationResult::kCRCError: return "crc_error";
    case NArchive::NExtract::NOperationResult::kUnavailable: return "unavailable";
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd: return "unexpected_end";
    case NArchive::NExtract::NOperationResult::kDataAfterEnd: return "data_after_end";
    case NArchive::NExtract::NOperationResult::kIsNotArc: return "is_not_arc";
    case NArchive::NExtract::NOperationResult::kHeadersError: return "headers_error";
    case NArchive::NExtract::NOperationResult::kWrongPassword: return "wrong_password";
    default: return "unknown";
  }
}

static const char *GetAskModeName(Int32 askMode)
{
  switch (askMode)
  {
    case NArchive::NExtract::NAskMode::kExtract: return "extract";
    case NArchive::NExtract::NAskMode::kTest: return "test";
    case NArchive::NExtract::NAskMode::kSkip: return "skip";
    case NArchive::NExtract::NAskMode::kReadExternal: return "read_external";
    default: return "unknown";
  }
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
  NCOM::CPropVariant prop;
  RINOK(archive->GetProperty(index, kpidIsDir, &prop))
  if (prop.vt == VT_BOOL)
    result = VARIANT_BOOLToBool(prop.boolVal);
  else if (prop.vt == VT_EMPTY)
    result = false;
  else
    return E_FAIL;
  return S_OK;
}

// Characters that Windows does not allow in file names. They are replaced with
// '_' in every path component, as 7-Zip does on Windows (ReplaceIncorrectChars
// in CPP/7zip/UI/Common/ExtractingFilePath.cpp), but on every platform, so the
// output names are the same on Linux and Windows. Installers use such names,
// for example "{code:GetDestDir}\app.exe".
static bool IsIncorrectFileNameChar(wchar_t c)
{
  return c < 0x20 || c == L':' || c == L'*' || c == L'?' || c == L'<'
      || c == L'>' || c == L'|' || c == L'"';
}

// Converts an item path to a relative path below the output folder. Both '\\'
// and '/' separate components. Returns false for absolute paths (a leading
// separator or a drive letter such as "C:") and for "..".
static bool GetSafeRelativePath(const UString &path, UString &result)
{
  result.Empty();
  if (path.Len() >= 2 && path[1] == L':'
      && ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')))
    return false; // drive letter
  UString part;
  for (unsigned i = 0; i <= path.Len(); i++)
  {
    const wchar_t c = (i < path.Len() ? path[i] : 0);
    if (c != 0 && c != L'\\' && c != L'/')
    {
      part += (IsIncorrectFileNameChar(c) ? L'_' : c);
      continue;
    }
    if (part.IsEmpty())
    {
      if (i == 0 && c != 0)
        return false; // absolute path
      continue;
    }
    if (part.IsEqualTo(".."))
      return false;
    if (!part.IsEqualTo("."))
    {
      if (!result.IsEmpty())
        result.Add_PathSepar();
      result += part;
    }
    part.Empty();
  }
  return !result.IsEmpty();
}


//////////////////////////////////////////////////////////////
// Archive open callback

class CArchiveOpenCallback Z7_final:
  public IArchiveOpenCallback,
  public IArchiveOpenVolumeCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_3(IArchiveOpenCallback, IArchiveOpenVolumeCallback, ICryptoGetTextPassword)
public:
  FString FolderPrefix;      // folder of the archive, with a trailing separator
  NFind::CFileInfo FileInfo; // the archive file
  bool PasswordIsDefined;
  UString Password;

  CArchiveOpenCallback(): PasswordIsDefined(false) {}
};

Z7_COM7F_IMF(CArchiveOpenCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */))
{
  return S_OK;
}

Z7_COM7F_IMF(CArchiveOpenCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */))
{
  return S_OK;
}

Z7_COM7F_IMF(CArchiveOpenCallback::GetProperty(PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidName: prop = fs2us(FileInfo.Name); break;
    case kpidIsDir: prop = FileInfo.IsDir(); break;
    case kpidSize: prop = FileInfo.Size; break;
    case kpidMTime: PropVariant_SetFrom_FiTime(prop, FileInfo.MTime); break;
    default: break;
  }
  return prop.Detach(value);
}

Z7_COM7F_IMF(CArchiveOpenCallback::GetStream(const wchar_t *name, IInStream **inStream))
{
  *inStream = NULL;
  const UString volName (name);
  if (volName.IsEmpty() || volName.Find(L'/') >= 0 || volName.Find(L'\\') >= 0)
    return S_FALSE;
  const FString fullPath = FolderPrefix + us2fs(volName);
  NFind::CFileInfo fi;
  if (!fi.Find(fullPath) || fi.IsDir())
    return S_FALSE;
  CInFileStream *inFileSpec = new CInFileStream;
  CMyComPtr<IInStream> inFile = inFileSpec;
  if (!inFileSpec->Open(fullPath))
    return GetLastError_noZero_HRESULT();
  *inStream = inFile.Detach();
  return S_OK;
}

Z7_COM7F_IMF(CArchiveOpenCallback::CryptoGetTextPassword(BSTR *password))
{
  if (!PasswordIsDefined)
  {
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}


//////////////////////////////////////////////////////////////
// Archive extract callback

static const wchar_t * const kEmptyFileAlias = L"[Content]";

class CArchiveExtractCallback Z7_final:
  public IArchiveExtractCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback, ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(IProgress)

  CMyComPtr<IInArchive> _archiveHandler;
  FString _directoryPath;  // output folder
  UInt32 _index;
  Int32 _askMode;
  UString _filePath;       // name inside the archive
  bool _mtimeDefined;
  FILETIME _mtime;

  COutFileStream *_outFileStreamSpec;
  CMyComPtr<ISequentialOutStream> _outFileStream;

public:
  UInt64 NumErrors;
  UInt64 NumItems;
  UInt64 NumSkipped;
  UInt64 Total;
  UInt64 Completed;
  bool TotalDefined;
  bool CompletedDecreased;
  bool PasswordIsDefined;
  bool SkipExisting;
  UString Password;

  CArchiveExtractCallback():
      _index(0),
      _askMode(0),
      _mtimeDefined(false),
      _outFileStreamSpec(NULL),
      NumErrors(0),
      NumItems(0),
      NumSkipped(0),
      Total(0),
      Completed(0),
      TotalDefined(false),
      CompletedDecreased(false),
      PasswordIsDefined(false),
      SkipExisting(false)
  {
    _mtime.dwLowDateTime = _mtime.dwHighDateTime = 0;
  }

  void Init(IInArchive *archiveHandler, const FString &directoryPath)
  {
    _archiveHandler = archiveHandler;
    _directoryPath = directoryPath;
    if (!_directoryPath.IsEmpty())
      NName::NormalizeDirPathPrefix(_directoryPath);
  }
};

Z7_COM7F_IMF(CArchiveExtractCallback::SetTotal(UInt64 size))
{
  Total = size;
  TotalDefined = true;
  return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetCompleted(const UInt64 *completeValue))
{
  if (completeValue)
  {
    if (*completeValue < Completed)
      CompletedDecreased = true;
    Completed = *completeValue;
  }
  return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::GetStream(UInt32 index,
    ISequentialOutStream **outStream, Int32 askExtractMode))
{
  *outStream = NULL;
  _outFileStream.Release();
  _outFileStreamSpec = NULL;
  _index = index;
  _askMode = askExtractMode;
  _mtimeDefined = false;

  {
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidPath, &prop))
    if (prop.vt == VT_EMPTY)
      _filePath = kEmptyFileAlias;
    else if (prop.vt == VT_BSTR)
      _filePath = prop.bstrVal;
    else
      return E_FAIL;
  }

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;

  {
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidMTime, &prop))
    if (prop.vt == VT_FILETIME)
    {
      _mtime = prop.filetime;
      _mtimeDefined = true;
    }
    else if (prop.vt != VT_EMPTY)
      return E_FAIL;
  }

  bool isDir = false;
  RINOK(IsArchiveItemFolder(_archiveHandler, index, isDir))

  UString relPath;
  if (!GetSafeRelativePath(_filePath, relPath))
  {
    PrintError("Unsafe path in archive", us2fs(_filePath));
    return E_ABORT;
  }

  {
    const int slashPos = relPath.ReverseFind_PathSepar();
    if (slashPos >= 0)
      CreateComplexDir(_directoryPath + us2fs(relPath.Left((unsigned)slashPos)));
  }

  const FString fullProcessedPath = _directoryPath + us2fs(relPath);

  if (isDir)
  {
    CreateComplexDir(fullProcessedPath);
    return S_OK;
  }

  NFind::CFileInfo fi;
  if (fi.Find(fullProcessedPath))
  {
    if (SkipExisting)
    {
      NumSkipped++;
      CJsonLine j("skipped");
      j.AddUInt64("index", index);
      j.Add("path", _filePath);
      j.Print();
      return S_OK;
    }
    if (!DeleteFileAlways(fullProcessedPath))
    {
      PrintError("Cannot delete output file", fullProcessedPath);
      return E_ABORT;
    }
  }

  _outFileStreamSpec = new COutFileStream;
  CMyComPtr<ISequentialOutStream> outStreamLoc(_outFileStreamSpec);
  if (!_outFileStreamSpec->Create_ALWAYS(fullProcessedPath))
  {
    PrintError("Cannot open output file", fullProcessedPath, GetLastError_noZero_HRESULT());
    return E_ABORT;
  }
  _outFileStream = outStreamLoc;
  *outStream = outStreamLoc.Detach();
  return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode))
{
  _askMode = askExtractMode;
  return S_OK;
}

Z7_COM7F_IMF(CArchiveExtractCallback::SetOperationResult(Int32 operationResult))
{
  NumItems++;
  if (operationResult != NArchive::NExtract::NOperationResult::kOK)
    NumErrors++;

  HRESULT closeRes = S_OK;
  if (_outFileStream)
  {
    if (_mtimeDefined)
    {
      CFiTime ft;
     #ifdef _WIN32
      ft = _mtime;
     #else
      FILETIME_To_timespec(_mtime, ft);
     #endif
      _outFileStreamSpec->SetMTime(&ft);
    }
    closeRes = _outFileStreamSpec->Close();
  }
  _outFileStream.Release();
  _outFileStreamSpec = NULL;

  CJsonLine j("item_result");
  j.AddUInt64("index", _index);
  j.Add("path", _filePath);
  j.Add("mode", GetAskModeName(_askMode));
  j.Add("result", GetOperationResultName(operationResult));
  if (operationResult != NArchive::NExtract::NOperationResult::kOK)
    j.AddInt64("result_code", operationResult);
  j.Print();
  return closeRes;
}

Z7_COM7F_IMF(CArchiveExtractCallback::CryptoGetTextPassword(BSTR *password))
{
  if (!PasswordIsDefined)
  {
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}


//////////////////////////////////////////////////////////////
// Plugin

static void GuidToString(const GUID &g, AString &s)
{
  char temp[16];
  s.Empty();
  ConvertUInt32ToHex8Digits(g.Data1, temp);
  s += temp;
  s += '-';
  ConvertUInt32ToHex8Digits(g.Data2, temp);
  s += temp + 4;
  s += '-';
  ConvertUInt32ToHex8Digits(g.Data3, temp);
  s += temp + 4;
  s += '-';
  for (unsigned i = 0; i < 8; i++)
  {
    if (i == 2)
      s += '-';
    ConvertUInt32ToHex8Digits(g.Data4[i], temp);
    s += temp + 6;
  }
}

static bool GetGuidProp(const PROPVARIANT &prop, GUID &guid)
{
  if (prop.vt != VT_BSTR || ::SysStringByteLen(prop.bstrVal) != sizeof(GUID))
    return false;
  memcpy(&guid, prop.bstrVal, sizeof(GUID));
  return true;
}

struct CPlugin
{
  NDLL::CLibrary Lib;
  Func_CreateObject CreateObject;
  Func_GetNumberOfFormats GetNumberOfFormats;
  Func_GetHandlerProperty2 GetHandlerProperty2;
  Func_GetIsArc GetIsArc;
  Func_GetModuleProp GetModuleProp;

  CPlugin():
      CreateObject(NULL),
      GetNumberOfFormats(NULL),
      GetHandlerProperty2(NULL),
      GetIsArc(NULL),
      GetModuleProp(NULL)
    {}

  bool Load(const FString &path)
  {
    if (!Lib.Load(path))
    {
      PrintError("Cannot load the plugin", path, GetLastError_noZero_HRESULT());
      return false;
    }
    const HMODULE h = Lib.Get_HMODULE();
    CreateObject = Z7_GET_PROC_ADDRESS(Func_CreateObject, h, "CreateObject");
    GetNumberOfFormats = Z7_GET_PROC_ADDRESS(Func_GetNumberOfFormats, h, "GetNumberOfFormats");
    GetHandlerProperty2 = Z7_GET_PROC_ADDRESS(Func_GetHandlerProperty2, h, "GetHandlerProperty2");
    GetIsArc = Z7_GET_PROC_ADDRESS(Func_GetIsArc, h, "GetIsArc");
    GetModuleProp = Z7_GET_PROC_ADDRESS(Func_GetModuleProp, h, "GetModuleProp");
    if (!CreateObject || !GetNumberOfFormats || !GetHandlerProperty2)
    {
      PrintError("The plugin does not export CreateObject, GetNumberOfFormats and GetHandlerProperty2", path);
      return false;
    }
    return true;
  }

  HRESULT FindFormat(const char *formatName, GUID &clsid)
  {
    UInt32 numFormats = 0;
    RINOK(GetNumberOfFormats(&numFormats))
    for (UInt32 i = 0; i < numFormats; i++)
    {
      {
        NCOM::CPropVariant prop;
        RINOK(GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop))
        if (prop.vt != VT_BSTR || !UString(prop.bstrVal).IsEqualTo_Ascii_NoCase(formatName))
          continue;
      }
      NCOM::CPropVariant prop;
      RINOK(GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop))
      if (!GetGuidProp(prop, clsid))
        return E_FAIL;
      return S_OK;
    }
    return S_FALSE;
  }
};

static int ListFormats(CPlugin &plugin)
{
  if (plugin.GetModuleProp)
  {
    CJsonLine j("module");
    NCOM::CPropVariant prop;
    if (plugin.GetModuleProp(NModulePropID::kVersion, &prop) == S_OK && prop.vt == VT_UI4)
    {
      AString s;
      s.Add_UInt32(prop.ulVal >> 16);
      s += '.';
      const UInt32 minor = prop.ulVal & 0xFFFF;
      if (minor < 10)
        s += '0';
      s.Add_UInt32(minor);
      j.Add("version", s);
    }
    j.Print();
  }

  UInt32 numFormats = 0;
  if (plugin.GetNumberOfFormats(&numFormats) != S_OK)
  {
    PrintError("GetNumberOfFormats failed");
    return k_Exit_Usage;
  }
  for (UInt32 i = 0; i < numFormats; i++)
  {
    CJsonLine j("format");
    j.AddUInt64("index", i);
    {
      NCOM::CPropVariant prop;
      plugin.GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop);
      j.AddProp("name", prop);
    }
    {
      NCOM::CPropVariant prop;
      plugin.GetHandlerProperty2(i, NArchive::NHandlerPropID::kExtension, &prop);
      j.AddProp("ext", prop);
    }
    {
      NCOM::CPropVariant prop;
      plugin.GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop);
      GUID clsid;
      if (GetGuidProp(prop, clsid))
      {
        AString s;
        GuidToString(clsid, s);
        j.Add("clsid", s);
      }
      else
        j.AddNull("clsid");
    }
    {
      NCOM::CPropVariant prop;
      plugin.GetHandlerProperty2(i, NArchive::NHandlerPropID::kFlags, &prop);
      if (prop.vt == VT_UI4)
        j.AddHex32("flags", prop.ulVal);
      else
        j.AddNull("flags");
    }
    {
      NCOM::CPropVariant prop;
      plugin.GetHandlerProperty2(i, NArchive::NHandlerPropID::kSignature, &prop);
      j.AddBool("has_signature", prop.vt == VT_BSTR && ::SysStringByteLen(prop.bstrVal) != 0);
    }
    if (plugin.GetIsArc)
    {
      Func_IsArc isArc = NULL;
      j.AddBool("has_is_arc", plugin.GetIsArc(i, &isArc) == S_OK && isArc != NULL);
    }
    j.Print();
  }
  return k_Exit_OK;
}

static void PrintProps(CJsonLine &j, IInArchive *archive, UInt32 index, bool archiveProps)
{
  CRecordVector<PROPID> ids;
  {
    UInt32 numProps = 0;
    if ((archiveProps ?
        archive->GetNumberOfArchiveProperties(&numProps) :
        archive->GetNumberOfProperties(&numProps)) != S_OK)
      numProps = 0;
    for (UInt32 i = 0; i < numProps; i++)
    {
      CMyComBSTR name;
      PROPID propID;
      VARTYPE vt;
      if ((archiveProps ?
          archive->GetArchivePropertyInfo(i, &name, &propID, &vt) :
          archive->GetPropertyInfo(i, &name, &propID, &vt)) == S_OK)
        ids.AddToUniqueSorted(propID);
    }
  }
  // Properties that 7-Zip reads even if they are not listed.
  if (archiveProps)
  {
    ids.AddToUniqueSorted(kpidPhySize);
    ids.AddToUniqueSorted(kpidWarning);
    ids.AddToUniqueSorted(kpidError);
    ids.AddToUniqueSorted(kpidErrorFlags);
  }
  else
  {
    ids.AddToUniqueSorted(kpidPath);
    ids.AddToUniqueSorted(kpidIsDir);
  }

  FOR_VECTOR (i, ids)
  {
    const PROPID propID = ids[i];
    NCOM::CPropVariant prop;
    const HRESULT res = archiveProps ?
        archive->GetArchiveProperty(propID, &prop) :
        archive->GetProperty(index, propID, &prop);
    const AString name = GetPropName(propID);
    if (res != S_OK)
    {
      AString s ("error 0x");
      char temp[16];
      ConvertUInt32ToHex8Digits((UInt32)res, temp);
      s += temp;
      j.Add(name, s);
    }
    else if (prop.vt != VT_EMPTY)
      j.AddProp(name, prop);
  }
}

struct COptions
{
  bool PasswordIsDefined;
  bool IndicesDefined;
  bool SkipExisting;
  UString Password;
  CRecordVector<UInt32> Indices;

  COptions(): PasswordIsDefined(false), IndicesDefined(false), SkipExisting(false) {}
};

// Parses "<index>[,<index>...]".
static bool ParseIndices(const char *s, CRecordVector<UInt32> &indices)
{
  for (;;)
  {
    const char *end;
    const UInt32 index = ConvertStringToUInt32(s, &end);
    if (end == s)
      return false;
    indices.Add(index);
    if (*end == 0)
      return true;
    if (*end != ',')
      return false;
    s = end + 1;
  }
}

static int ProcessArchive(CPlugin &plugin, char command, const FString &archiveName,
    const FString &outDir, const COptions &options)
{
  GUID clsid;
  {
    const HRESULT res = plugin.FindFormat(kFormatName, clsid);
    if (res != S_OK)
    {
      PrintError("The plugin has no Inno format", FString(), res);
      return k_Exit_Usage;
    }
  }

  CMyComPtr<IInArchive> archive;
  {
    const HRESULT res = plugin.CreateObject(&clsid, &IID_IInArchive, (void **)&archive);
    if (res != S_OK || !archive)
    {
      PrintError("CreateObject failed", FString(), res);
      return k_Exit_Usage;
    }
  }

  CArchiveOpenCallback *openCallbackSpec = new CArchiveOpenCallback;
  CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
  openCallbackSpec->PasswordIsDefined = options.PasswordIsDefined;
  openCallbackSpec->Password = options.Password;
  {
    FString fileName;
    if (!GetFullPathAndSplit(archiveName, openCallbackSpec->FolderPrefix, fileName)
        || !openCallbackSpec->FileInfo.Find(archiveName)
        || openCallbackSpec->FileInfo.IsDir())
    {
      PrintError("Cannot find archive file", archiveName);
      return k_Exit_Open;
    }
  }

  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> file = fileSpec;
  if (!fileSpec->Open(archiveName))
  {
    PrintError("Cannot open archive file", archiveName, GetLastError_noZero_HRESULT());
    return k_Exit_Open;
  }

  {
    const UInt64 scanSize = 1 << 23;
    const HRESULT res = archive->Open(file, &scanSize, openCallback);
    if (res != S_OK)
    {
      PrintError("Cannot open file as archive", archiveName, res);
      return k_Exit_Open;
    }
  }

  int exitCode = k_Exit_OK;

  if (command == 'l')
  {
    {
      CJsonLine j("archive");
      PrintProps(j, archive, 0, true);
      j.Print();
    }
    UInt32 numItems = 0;
    archive->GetNumberOfItems(&numItems);
    for (UInt32 i = 0; i < numItems; i++)
    {
      CJsonLine j("item");
      j.AddUInt64("index", i);
      PrintProps(j, archive, i, false);
      j.Print();
    }
  }
  else
  {
    const bool testMode = (command == 't');
    if (!testMode && !CreateComplexDir(outDir))
    {
      PrintError("Cannot create output folder", outDir, GetLastError_noZero_HRESULT());
      return k_Exit_Usage;
    }

    CArchiveExtractCallback *extractCallbackSpec = new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> extractCallback(extractCallbackSpec);
    extractCallbackSpec->Init(archive, outDir);
    extractCallbackSpec->PasswordIsDefined = options.PasswordIsDefined;
    extractCallbackSpec->Password = options.Password;
    extractCallbackSpec->SkipExisting = options.SkipExisting;

    const HRESULT result = options.IndicesDefined ?
        archive->Extract(options.Indices.ConstData(), options.Indices.Size(), testMode ? 1 : 0, extractCallback) :
        archive->Extract(NULL, (UInt32)(Int32)(-1), testMode ? 1 : 0, extractCallback);

    CJsonLine j("extract_result");
    j.Add("mode", testMode ? "test" : "extract");
    j.AddHex32("hresult", (UInt32)result);
    j.AddUInt64("items", extractCallbackSpec->NumItems);
    j.AddUInt64("errors", extractCallbackSpec->NumErrors);
    j.AddUInt64("skipped", extractCallbackSpec->NumSkipped);
    if (extractCallbackSpec->TotalDefined)
      j.AddUInt64("total", extractCallbackSpec->Total);
    j.AddUInt64("completed", extractCallbackSpec->Completed);
    j.AddBool("completed_decreased", extractCallbackSpec->CompletedDecreased);
    j.Print();

    if (result != S_OK)
      exitCode = k_Exit_Extract;
    else if (extractCallbackSpec->NumErrors != 0)
      exitCode = k_Exit_ItemErrors;
  }

  archive->Close();
  return exitCode;
}

static FString CmdStringToFString(const char *s)
{
  return us2fs(GetUnicodeString(s));
}

#if defined(_UNICODE) && !defined(_WIN64) && !defined(UNDER_CE)
#define NT_CHECK_FAIL_ACTION fputs("Unsupported Windows version\n", stderr); return 1;
#endif

int Z7_CDECL main(int numArgs, const char *args[])
{
  NT_CHECK

  FString pluginPath;
  char command = 0;
  COptions options;
  CObjectVector<FString> params;

  for (int i = 1; i < numArgs; i++)
  {
    const AString a (args[i]);
    if (a.IsPrefixedBy("-p") && !options.PasswordIsDefined)
    {
      options.Password = GetUnicodeString(a.Ptr(2));
      options.PasswordIsDefined = true;
    }
    else if (a.IsPrefixedBy("-i") && !options.IndicesDefined)
    {
      if (!ParseIndices(a.Ptr(2), options.Indices))
      {
        fputs(kHelpString, stderr);
        return k_Exit_Usage;
      }
      options.IndicesDefined = true;
    }
    else if (a == "-s" && !options.SkipExisting)
      options.SkipExisting = true;
    else if (pluginPath.IsEmpty())
      pluginPath = CmdStringToFString(a);
    else if (command == 0 && a.Len() == 1)
      command = (char)MyCharLower_Ascii(a[0]);
    else
      params.Add(CmdStringToFString(a));
  }

  unsigned numParams = 0;
  switch (command)
  {
    case 'i': numParams = 0; break;
    case 'l': numParams = 1; break;
    case 't': numParams = 1; break;
    case 'x': numParams = 2; break;
    default:
      fputs(kHelpString, stderr);
      return k_Exit_Usage;
  }
  if (params.Size() != numParams
      || (options.IndicesDefined && command != 't' && command != 'x')
      || (options.SkipExisting && command != 'x'))
  {
    fputs(kHelpString, stderr);
    return k_Exit_Usage;
  }

  // dlopen() searches the library path for names without a slash.
  {
    FString fullPath;
    if (NName::GetFullPath(pluginPath, fullPath))
      pluginPath = fullPath;
  }

  CPlugin plugin;
  if (!plugin.Load(pluginPath))
    return k_Exit_Usage;

  if (command == 'i')
    return ListFormats(plugin);

  return ProcessArchive(plugin, command, params[0],
      numParams > 1 ? params[1] : FString(), options);
}
