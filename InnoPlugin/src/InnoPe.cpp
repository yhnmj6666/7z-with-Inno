// InnoPe.cpp

#include "StdAfx.h"

#include <string.h>

#include "C/CpuArch.h"

#include "CPP/7zip/Archive/IArchive.h"

#include "InnoPe.h"

#define Get16(p) GetUi16(p)
#define Get32(p) GetUi32(p)

namespace NArchive {
namespace NInno {

static const UInt32 kRcData = 10;
static const UInt32 kLoaderResourceName = 11111;
static const UInt32 kSubdirFlag = (UInt32)1 << 31;
static const unsigned kSectionSize = 40;

// The DOS header of setup loaders before Inno Setup 5.1.5 holds "Inno", the
// offset of the offset table and its complement at 0x30.
static const UInt32 kLegacyMarker = 0x6f6e6e49;
static const unsigned kLegacyMarkerPos = 0x30;

// Offset table identifiers that start the resource data (loader/offsets.cpp).
static const char * const kLoaderIds[] =
{
  "rDlPtS",
  "nS5W7dT"
};

static bool IsAvail(size_t size, UInt64 pos, UInt64 len)
{
  return pos <= size && len <= size - pos;
}

UInt32 ParsePeHeaders(const Byte *p, size_t size, CPeHeaders &h)
{
  h.NumSections = 0;
  h.SectionsPos = 0;
  h.ResourceRva = 0;
  h.CertPos = 0;
  h.CertSize = 0;
  if (size < 0x40)
    return k_IsArc_Res_NEED_MORE;
  if (p[0] != 'M' || p[1] != 'Z')
    return k_IsArc_Res_NO;
  // innoextract reads e_lfanew as 16-bit value.
  const UInt32 pe = Get32(p + 0x3C);
  if (pe < 0x40 || pe > 0xFFFF)
    return k_IsArc_Res_NO;
  if (!IsAvail(size, pe, 24 + 2))
    return k_IsArc_Res_NEED_MORE;
  if (Get32(p + pe) != 0x4550)
    return k_IsArc_Res_NO;
  const UInt32 numSections = Get16(p + pe + 6);
  const UInt32 optSize = Get16(p + pe + 20);
  const UInt32 opt = pe + 24;
  const UInt32 magic = Get16(p + opt);
  UInt32 numDirsPos;
  if (magic == 0x10b)
    numDirsPos = 92;
  else if (magic == 0x20b)
    numDirsPos = 108;
  else
    return k_IsArc_Res_NO;
  if (optSize < numDirsPos + 4)
    return k_IsArc_Res_NO;
  if (!IsAvail(size, opt, optSize))
    return k_IsArc_Res_NEED_MORE;
  UInt32 numDirs = Get32(p + opt + numDirsPos);
  const UInt32 maxDirs = (optSize - numDirsPos - 4) / 8;
  if (numDirs > maxDirs)
    numDirs = maxDirs;
  const Byte *dirs = p + opt + numDirsPos + 4;
  if (numDirs > 2)
    h.ResourceRva = Get32(dirs + 2 * 8);
  if (numDirs > 4)
  {
    h.CertPos = Get32(dirs + 4 * 8);
    h.CertSize = Get32(dirs + 4 * 8 + 4);
    if (h.CertPos == 0)
      h.CertSize = 0;
  }
  h.NumSections = numSections;
  h.SectionsPos = (size_t)opt + optSize;
  if (!IsAvail(size, h.SectionsPos, (UInt64)numSections * kSectionSize))
    return k_IsArc_Res_NEED_MORE;
  return k_IsArc_Res_YES;
}

static bool RvaToPos(const Byte *p, const CPeHeaders &h, UInt32 rva, UInt32 &pos)
{
  for (UInt32 i = 0; i < h.NumSections; i++)
  {
    const Byte *s = p + h.SectionsPos + (size_t)i * kSectionSize;
    const UInt32 vSize = Get32(s + 8);
    const UInt32 va = Get32(s + 12);
    const UInt32 rawPos = Get32(s + 20);
    if (rva >= va && rva - va < vSize)
    {
      pos = rva - va + rawPos;
      return pos >= rawPos;
    }
  }
  return false;
}

// Finds the entry with the given id (or the first entry if id is 0) in the
// resource directory at dirPos. Returns its data field in entry.
static UInt32 FindResourceEntry(const Byte *p, size_t size, UInt32 dirPos, UInt32 id,
    UInt32 &entry)
{
  if (!IsAvail(size, dirPos, 16))
    return k_IsArc_Res_NEED_MORE;
  const UInt32 numNamed = Get16(p + dirPos + 12);
  const UInt32 numIds = Get16(p + dirPos + 14);
  const UInt64 entries = (UInt64)dirPos + 16;
  if (id == 0)
  {
    if (numNamed + numIds == 0)
      return k_IsArc_Res_NO;
    if (!IsAvail(size, entries, 8))
      return k_IsArc_Res_NEED_MORE;
    entry = Get32(p + (size_t)entries + 4);
    return k_IsArc_Res_YES;
  }
  for (UInt32 i = numNamed; i < numNamed + numIds; i++)
  {
    const UInt64 e = entries + (UInt64)i * 8;
    if (!IsAvail(size, e, 8))
      return k_IsArc_Res_NEED_MORE;
    if (Get32(p + (size_t)e) == id)
    {
      entry = Get32(p + (size_t)e + 4);
      return k_IsArc_Res_YES;
    }
  }
  return k_IsArc_Res_NO;
}

UInt32 FindLoaderResource(const Byte *p, size_t size, const CPeHeaders &h,
    UInt32 &resPos, UInt32 &resSize)
{
  resPos = 0;
  resSize = 0;
  UInt32 base;
  if (h.ResourceRva == 0 || !RvaToPos(p, h, h.ResourceRva, base))
    return k_IsArc_Res_NO;
  const UInt32 ids[3] = { kRcData, kLoaderResourceName, 0 };
  UInt32 dirPos = base;
  UInt32 entry = 0;
  for (unsigned level = 0; level < 3; level++)
  {
    const UInt32 res = FindResourceEntry(p, size, dirPos, ids[level], entry);
    if (res != k_IsArc_Res_YES)
      return res;
    const bool isDir = (entry & kSubdirFlag) != 0;
    if (isDir != (level != 2))
      return k_IsArc_Res_NO;
    dirPos = base + (entry & ~kSubdirFlag);
    if (dirPos < base)
      return k_IsArc_Res_NO;
  }
  // dirPos is the position of the data entry now.
  if (!IsAvail(size, dirPos, 8))
    return k_IsArc_Res_NEED_MORE;
  if (!RvaToPos(p, h, Get32(p + dirPos), resPos))
    return k_IsArc_Res_NO;
  resSize = Get32(p + dirPos + 4);
  return k_IsArc_Res_YES;
}

UInt32 IsSetupLoader(const Byte *p, size_t size)
{
  if (size < 2)
    return k_IsArc_Res_NEED_MORE;
  if (p[0] != 'M' || p[1] != 'Z')
    return k_IsArc_Res_NO;
  if (size < 0x40)
    return k_IsArc_Res_NEED_MORE;
  if (Get32(p + kLegacyMarkerPos) == kLegacyMarker
      && Get32(p + kLegacyMarkerPos + 4) == ~Get32(p + kLegacyMarkerPos + 8))
    return k_IsArc_Res_YES;

  CPeHeaders h;
  UInt32 res = ParsePeHeaders(p, size, h);
  if (res != k_IsArc_Res_YES)
    return res;
  UInt32 resPos, resSize;
  res = FindLoaderResource(p, size, h, resPos, resSize);
  if (res != k_IsArc_Res_YES)
    return res;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kLoaderIds); i++)
  {
    const size_t len = strlen(kLoaderIds[i]);
    if (resSize < len)
      continue;
    if (!IsAvail(size, resPos, len))
      return k_IsArc_Res_NEED_MORE;
    if (memcmp(p + resPos, kLoaderIds[i], len) == 0)
      return k_IsArc_Res_YES;
  }
  return k_IsArc_Res_NO;
}

}}
