// InnoHandler.h

#ifndef __INNO_HANDLER_H
#define __INNO_HANDLER_H

#include "CPP/Common/MyCom.h"

#include "CPP/7zip/Common/CreateCoder.h"

#include "CPP/7zip/Archive/IArchive.h"

#include "InnoIn.h"

//#include "inno/loader/offsets.hpp"
#include <sstream>

namespace NArchive {
namespace NInno {

Z7_CLASS_IMP_CHandler_IInArchive_0

	CInArchive _archive;
	//AString _methodString;

	//bool GetUncompressedSize(unsigned index, UInt32 &size) const;
	//bool GetCompressedSize(unsigned index, UInt32 &size) const;

	// AString GetMethod(NMethodType::EEnum method, bool useItemFilter, UInt32 dictionary) const;
};

API_FUNC_IsArc IsArc_Inno(const Byte* p, size_t size);

}}

#endif
