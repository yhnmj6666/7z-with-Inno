// InnoPe.h
// Bounds-checked parsing of the PE headers of a setup loader in a memory
// buffer. The functions return k_IsArc_Res_YES on success, k_IsArc_Res_NO if
// the data does not match and k_IsArc_Res_NEED_MORE if the buffer is too short.

#ifndef ZIP7_INC_INNO_PE_H
#define ZIP7_INC_INNO_PE_H

#include "CPP/Common/MyTypes.h"

#include <stddef.h>

namespace NArchive {
namespace NInno {

struct CPeHeaders
{
  UInt32 NumSections;
  size_t SectionsPos;    // offset of the section table in the buffer
  UInt32 ResourceRva;    // 0 if there is no resource directory
  UInt32 CertPos;        // file offset of the certificate table (signature)
  UInt32 CertSize;       // 0 if the file is not signed
};

UInt32 ParsePeHeaders(const Byte *p, size_t size, CPeHeaders &h);

// Finds the RT_RCDATA resource 11111 that holds the offset table of the
// setup loader, like innoextract (first language).
UInt32 FindLoaderResource(const Byte *p, size_t size, const CPeHeaders &h,
    UInt32 &resPos, UInt32 &resSize);

// Checks for an Inno Setup setup loader: the offset table referenced from the
// DOS header (before Inno Setup 5.1.5) or stored as resource 11111.
UInt32 IsSetupLoader(const Byte *p, size_t size);

}}

#endif
