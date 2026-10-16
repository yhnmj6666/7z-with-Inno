// InnoIn.h

#ifndef __ARCHIVE_Inno_IN_H
#define __ARCHIVE_Inno_IN_H

#include "../../../../C/CpuArch.h"

#include "../../../Common/DynLimBuf.h"
#include "../../../Common/MyBuffer.h"
#include "../../../Common/MyCom.h"
#include "../../../Common/StringConvert.h"
#include "../../../Common/UTFConvert.h"
#include "../IArchive.h"

#include "InnoDecode.h"
#include "InnoInterop.h"
#include "InnoItem.h"

#include "./InnoExtract/loader/offsets.hpp"
#include "./InnoExtract/setup/file.hpp"
#include "./InnoExtract/setup/info.hpp"
#include "./InnoExtract/stream/slice.hpp"

#include <memory>
#include <set>

namespace NArchive {
namespace NInno {

class NotImplemented : public std::logic_error
{
public:
	NotImplemented(std::string s) : std::logic_error("Function `" + s + "` not yet implemented") { };
};

using std::istream;
using std::unique_ptr;
using std::set;

class CEmbeddedFiles {
public:
	CEmbeddedFiles() : _pinfo(nullptr), _hasCollision(false), _time(0) {}
	CEmbeddedFiles(setup::info& info, bool hasCollision, FILETIME time);
	size_t GetNumberOfItems() const;
	CEmbeddedItem GetItem(size_t index) const;
	HRESULT ExtractItem(size_t indice, bool testMode, InteropFileStreamProgressWriter* outStream) const;
private:
	const setup::info* _pinfo;
	bool _hasCollision;
	int64_t _time;
	CEmbeddedItem GenerateInstallScript() const;
	size_t GenerateInstallScriptContent(std::string& outData) const;
	CEmbeddedItem GenerateLicense() const;
	size_t GenerateLicenseContent(std::string& outData) const;
	CEmbeddedItem GenerateLanguageFile(size_t langIndex) const;
	size_t GenerateLanguageFileContent(size_t langIndex, std::string& outData) const;
	CEmbeddedItem GenerateLanguageLicense(size_t langIndex) const;
	size_t GenerateLanguageLicenseContent(size_t langIndex, std::string& outData) const;
	CEmbeddedItem GenerateWizardImage(size_t imageIndex) const;
	size_t GenerateWizardImageContent(size_t imageIndex, std::string& outData) const;
	CEmbeddedItem GenerateWizardImageSmall(size_t imageIndex) const;
	size_t GenerateWizardImageSmallContent(size_t imageIndex, std::string& outData) const;
	const char* const GetMethod() const;
};

class CInArchive {

public:
	static void InitializeLog(std::string& filename);
	static std::string GetLogPath();
	HRESULT Open(IInStream* inStream, FILETIME modifiedTime);

	UInt32 GetDataOffset();
	UInt32 GetNumberOfItems();
	UInt32 GetHeaderSize();
	const char* const GetMethod();
	std::string GetComment();
	std::shared_ptr<CAbstractItem> GetItem(size_t index);
	CInArchiveReadProgressQueryHelper GetReadProgressQueryHelper() const;
	bool CheckCollideLocations(UInt32 index, UInt32& firstAppearance);
	HRESULT ExtractItem(UInt32 indice, bool testMode, InteropFileStreamProgressWriter* outStream, std::string* savedBuffer = nullptr);

	using chunk_reader = boost::iostreams::chain<boost::iostreams::input>;
	friend class CInArchiveReadProgressQueryHelper;
private:
	FILETIME _modifiedTime;
	std::string password;
	CEmbeddedFiles _embedded;
	unique_ptr<InteropStream> _pInteropStream;
	unique_ptr<istream> _pInStream;
	unique_ptr<stream::slice_reader> _pSliceReader;
	unique_ptr<chunk_reader> _pChunkReader;
	std::map<size_t, size_t> _collisionLocations; // map of index to first appearance of that index of file entry which has same index into data entry
	stream::chunk _chunk;
	loader::offsets _offsets;
	setup::info _info;

	bool _extractInitialized = false;
	bool _hasCollision = false;

	bool OpenInfo();
	void PrepareExtraction();
	void FindCollideLocation();
	chunk_reader* OpenChunk(stream::chunk& chunk);
	chunk_reader* ResetChunkReader(stream::chunk& chunk);
};

class CInArchiveReadProgressQueryHelper
{
private:
	const CInArchive* _archive;
public:
	CInArchiveReadProgressQueryHelper(const CInArchive* archive);
	UInt64 TotalRead() const;
};

}
}
  
#endif
