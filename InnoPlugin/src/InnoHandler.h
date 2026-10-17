// InnoHandler.h

#ifndef ZIP7_INC_INNO_HANDLER_H
#define ZIP7_INC_INNO_HANDLER_H

#include "CPP/Common/MyCom.h"

#include "CPP/7zip/Archive/IArchive.h"

#include "InnoIn.h"

namespace NArchive {
namespace NInno {

Z7_CLASS_IMP_CHandler_IInArchive_0

  CInArchive _arc;
  FILETIME _mtime;
  bool _mtimeDefined;

  HRESULT Extract2(const UInt32 *indices, UInt32 numItems, bool testMode,
      IArchiveExtractCallback *extractCallback);
public:
  CHandler(): _mtimeDefined(false) { _mtime.dwLowDateTime = 0; _mtime.dwHighDateTime = 0; }
};

API_FUNC_IsArc IsArc_Inno(const Byte *p, size_t size);

}}

#endif
