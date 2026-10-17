// InnoRegister.cpp

#include "StdAfx.h"

#include "CPP/7zip/Common/RegisterArc.h"

#include "InnoHandler.h"

namespace NArchive {
namespace NInno {

REGISTER_ARC_I_NO_SIG(
  "Inno", "exe", 0, 0x70,
  0,
  NArcInfoFlags::kPureStartOpen |
  NArcInfoFlags::kUseGlobalOffset,
  IsArc_Inno)

}}
