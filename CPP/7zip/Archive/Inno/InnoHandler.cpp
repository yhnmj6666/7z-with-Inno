// InnoHandler.cpp

#include "StdAfx.h"

#include "../../../../C/CpuArch.h"

#include "../../../Common/ComTry.h"
#include "../../../Common/IntToString.h"

#include "../../../Windows/PropVariant.h"

#include "../../Common/ProgressUtils.h"
#include "../../Common/StreamUtils.h"

#include "../Common/ItemNameUtils.h"

#include "../../Compress/CopyCoder.h"

#include "InnoHandler.h"
#include <map>

#define Get32(p) GetUi32(p)

#pragma warning( push )
#pragma warning( disable : 4100 )

using namespace NWindows;

namespace NArchive {
namespace NInno {

static const Byte kProps[] =
{
  kpidPath,
  kpidSize,
  kpidMTime,
  kpidAttrib,
  kpidCommented,
  kpidEncrypted,
  kpidCRC
};

static const Byte kArcProps[] =
{
  kpidMethod,
  kpidSolid,
  kpidCommented,
  kpidHeadersSize,
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps

// Hosts compile IInArchive methods as non-throwing, so no C++ exception may
// escape Open() or Extract() into the 7-Zip process (function-try-blocks below).
STDMETHODIMP CHandler::Open(IInStream* stream, const UInt64* /*maxCheckStartPosition*/, IArchiveOpenCallback* openArchiveCallback )
try
{
	IArchiveOpenVolumeCallback* ovc=nullptr;
	FILETIME ft = {};
	std::string fileName;
	if(openArchiveCallback->QueryInterface(IID_IArchiveOpenVolumeCallback,(void**)&ovc)==S_OK)
	{
		if (ovc)
		{
			NCOM::CPropVariant prop;
			if (ovc->GetProperty(kpidMTime, &prop) == S_OK)
			{
				if (prop.vt == VT_FILETIME)
				{
					ft = prop.filetime;
				}
			}
			if (ovc->GetProperty(kpidName, &prop) == S_OK)
            {
                if (prop.vt == VT_BSTR)
                {
					fileName = UnicodeStringToMultiByte(prop.bstrVal);
					CInArchive::InitializeLog(fileName);
                }
            }
		}
	}
	return _archive.Open(stream, ft);
}
catch (const std::bad_alloc&)
{
	return E_OUTOFMEMORY;
}
catch (...)
{
	// 7-Zip offers every .exe to this handler before PE, so a parse failure only means "not Inno".
	return S_FALSE;
}

STDMETHODIMP CHandler::Close()
{
	return S_OK;
}

STDMETHODIMP CHandler::GetNumberOfItems(UInt32* numItems)
{
	*numItems = _archive.GetNumberOfItems();
	return S_OK;
}

STDMETHODIMP CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
{
	//complete property
	COM_TRY_BEGIN
	NCOM::CPropVariant prop;
	auto item = _archive.GetItem(index);
	switch (propID)
	{
	case kpidPath:
		prop = MultiByteToUnicodeString(item->GetPath().data());
		break;
	case kpidSize:
		prop = item->GetSize();
		break;
	case kpidMTime:
		prop = item->GetModifiedTime();
		break;
	case kpidAttrib:
		prop = item->GetAttrib();
		break;
	case kpidCommented:
		prop = AString(item->GetComment().data());
		break;
	case kpidEncrypted:
		prop = item->GetEnryption();
		break;
	case kpidCRC:
		prop = item->GetChecksum();
		break;
	default:
		break;
	}
	prop.Detach(value);
	return S_OK;
	COM_TRY_END
}

STDMETHODIMP CHandler::Extract(const UInt32* indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback* extractCallback)
try
{
	if (numItems == 0)
		return S_OK;
	const bool allFilesMode = (numItems == (UInt32)(-1));

	Int32 askMode = testMode ?
		NExtract::NAskMode::kTest :
		NExtract::NAskMode::kExtract;
	CMyComPtr<ISequentialOutStream> realOutStream;
	if (allFilesMode)
	{
		numItems = _archive.GetNumberOfItems();
	}
	//trim indices
	std::map<UInt32, std::string> reuseBuffer;
	UInt64 totalSize = 0;
	for (UInt32 i = numItems - 1; i > 0; i--)
	{
		UInt32 index = allFilesMode ? i : indices[i];
		UInt32 firstAppearance = (UInt32)(-1);
		if (_archive.CheckCollideLocations(index, firstAppearance))
		{
			if (firstAppearance < numItems)
			{
				reuseBuffer.emplace(firstAppearance,std::string());
			}
		}
		totalSize+=_archive.GetItem(index)->GetSize();
	}
	extractCallback->SetTotal(totalSize);
	CLocalProgress* lps=new CLocalProgress();
	lps->Init(extractCallback, false);
	auto queryHelper(_archive.GetReadProgressQueryHelper());
	unique_ptr<InteropFileStreamProgressWriter> writer=std::make_unique<InteropFileStreamProgressWriter>(InteropFileStreamProgressWriter(lps, queryHelper));

	for (UInt32 i = 0; i < numItems; i++)
	{
		UInt32 index = allFilesMode ? i : indices[i];
		RINOK(extractCallback->GetStream(index, &realOutStream, askMode));
		if (realOutStream != NULL)
		{
			writer->SetOutStream(realOutStream);
			RINOK(extractCallback->PrepareOperation(askMode));
			UInt32 previousUse = (UInt32)(-1);
			HRESULT result = E_FAIL;
			if (_archive.CheckCollideLocations(index, previousUse))
			{
				if (previousUse < numItems)
				{
					std::string& buffer = reuseBuffer[previousUse];
					if (buffer.empty())
					{
						// if collide but the buffer is empty, we fill the buffer.
						result = _archive.ExtractItem(index, testMode, writer.get(), &buffer);
					}
					else
					{
						result = writer->Write(buffer.data(), (UInt32)buffer.size());
					}
				}
			}
			else 
			{
				auto shouldReuse = reuseBuffer.find(index);
				if (shouldReuse != reuseBuffer.end())
					result = _archive.ExtractItem(index, testMode, writer.get(), &shouldReuse->second);
				else
					result = _archive.ExtractItem(index, testMode, writer.get());
			}
			switch (result)
			{
			case S_OK:
				extractCallback->SetOperationResult(NExtract::NOperationResult::kOK);
				break;
			case E_OUTOFMEMORY:
				extractCallback->SetOperationResult(NExtract::NOperationResult::kUnavailable);
				break;
			case ERROR_INCORRECT_SIZE:
			case ERROR_DATA_CHECKSUM_ERROR:
			case E_FAIL:
			default:
				extractCallback->SetOperationResult(NExtract::NOperationResult::kDataError);
				break;
			}
		}
		else
			return E_FAIL;
	}
	return S_OK;
}
catch (const NotImplemented&)
{
	return E_NOTIMPL;
}
catch (const std::bad_alloc&)
{
	return E_OUTOFMEMORY;
}
catch (...)
{
	return E_FAIL;
}

STDMETHODIMP CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT* value)
{
	//complete archive property
	COM_TRY_BEGIN
	NCOM::CPropVariant prop;
	switch (propID)
	{
	case kpidSolid:
		prop = true;
		break;
	case kpidCommented:
		prop = MultiByteToUnicodeString(_archive.GetComment().data());
		break;
	case kpidMethod:
		prop = AString(_archive.GetMethod());
		break;
	case kpidHeadersSize:
		prop = _archive.GetHeaderSize();
		break;
	case kpidWarning:
		prop = AString(("Check debug log at: " + _archive.GetLogPath()).c_str());
		break;
	default:
		break;
	}
	prop.Detach(value);
	return S_OK;
	COM_TRY_END
}

API_FUNC_IsArc IsArc_Inno(const Byte* p, size_t size)
{	
	// TODO: judge archive
	return k_IsArc_Res_NO;
}

}
}

#pragma warning( pop )