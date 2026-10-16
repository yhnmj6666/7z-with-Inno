// NsisIn.cpp

#include "StdAfx.h"

#include "../../../Common/IntToString.h"
#include "../../../Common/StringToInt.h"
#include "../../Common/LimitedStreams.h"
#include "../../Common/StreamUtils.h"
#include "../../../Windows/TimeUtils.h"
#include "../../IPassword.h"

#include "InnoIn.h"
#include "InnoUtils.h"

#include "./InnoExtract/setup/type.hpp"
#include "./InnoExtract/setup/component.hpp"
#include "./InnoExtract/setup/task.hpp"
#include "./InnoExtract/setup/directory.hpp"
#include "./InnoExtract/setup/file.hpp"
#include "./InnoExtract/setup/icon.hpp"
#include "./InnoExtract/setup/ini.hpp"
#include "./InnoExtract/setup/delete.hpp"
#include "./InnoExtract/setup/language.hpp"
#include "./InnoExtract/setup/message.hpp"
#include "./InnoExtract/setup/registry.hpp"
#include "./InnoExtract/setup/run.hpp"
#include "./InnoExtract/stream/chunk.hpp"
#include "./InnoExtract/util/load.hpp"
#include "./InnoExtract/util/log.hpp"
#include "./InnoExtract/lib/magic_enum.hpp"

#include <sstream>
#include <filesystem>
#include <unordered_set>

#define Get16(p) GetUi16(p)
#define Get32(p) GetUi32(p)

// #define NUM_SPEED_TESTS 1000

namespace NArchive {
namespace NInno {

static const char* const kMethods[] =
{
	"Copy"
  , "Deflate"
  , "BZip2"
  , "LZMA"
  , "LZMA2"
  , "Unknown"
};

using std::make_unique;

constexpr std::string OSS_Clean(std::string s) noexcept {
	size_t last_dot = s.find_last_of('.');
	std::string property_name = last_dot != std::string::npos ? s.substr(last_dot + 1) : s;
	// copied from https://codereview.stackexchange.com/a/263761
	bool tail = false;
	std::size_t n = 0;
	for (unsigned char c : property_name) {
		if (c == '-' || c == '_') {
			tail = false;
		}
		else if (tail) {
			property_name[n++] = c;
		}
		else {
			tail = true;
			property_name[n++] = (char)std::toupper(c);
		}
	}
	property_name.resize(n);
	return property_name;
}

constexpr std::string Flag_Clean(std::string s)
{
	auto wit = s.begin();
	bool write = false;
	for (auto rit = s.begin(); rit != s.end(); rit++)
	{
		if (isalnum(*rit))
		{
			if (write) 
			{
				*wit = *rit;
			}
			wit++;
		}
		else
		{
			write = true;
		}
	}
	s.resize(wit - s.begin());
	return s;
}

void CInArchive::InitializeLog(std::string& filename)
{
	namespace fs = std::filesystem;
	const std::chrono::time_point now{ std::chrono::system_clock::now() };
	const time_t now_c = std::chrono::system_clock::to_time_t(now);
	const tm now_tm = *std::localtime(&now_c);
	std::ostringstream iss;
	iss << filename << "_" << std::put_time(&now_tm, "%Y%m%d-%H%M%S") << ".log";
	fs::path tempDir = fs::temp_directory_path();
	fs::path logPath = tempDir / iss.str();
	logger::log_path = logPath;
}

std::string CInArchive::GetLogPath()
{
    return logger::log_path.has_value() ? logger::log_path.value().string() : std::string();
}

HRESULT CInArchive::Open(IInStream* stream, FILETIME modifiedTime)
{
	_modifiedTime = modifiedTime;
	_pInteropStream = make_unique<InteropStream>(stream);
	_pInStream = make_unique<istream>(_pInteropStream.get());
	_offsets.load(*_pInStream);
	if (_offsets.found_magic)
	{
		if (OpenInfo())
		{
			FindCollideLocation();			
			_embedded = CEmbeddedFiles(_info, _hasCollision, _modifiedTime);
			return S_OK;
		}
		else
			return S_FALSE;
	}
	return S_FALSE;
}

UInt32 CInArchive::GetDataOffset()
{
	return _offsets.data_offset;
}

UInt32 CInArchive::GetNumberOfItems()
{
	return static_cast<UInt32>(_info.files.size() + _embedded.GetNumberOfItems());
}

UInt32 CInArchive::GetHeaderSize()
{
	return static_cast<UInt32>(_info.header.header_size);
}

const char* const CInArchive::GetMethod()
{
	switch (_info.header.compression)
	{
	case stream::compression_method::Stored:
		return kMethods[0];
	case stream::compression_method::Zlib:
		return kMethods[1];
	case stream::compression_method::BZip2:
		return kMethods[2];
	case stream::compression_method::LZMA1:
		return kMethods[3];
	case stream::compression_method::LZMA2:
		return kMethods[4];
	case stream::compression_method::UnknownCompression:
	default:
		return kMethods[5];
	}
}

std::string CInArchive::GetComment()
{
	std::ostringstream oss;

	oss << "InnoSetupVersion = " << _info.version << std::endl;

#define _OSS(s) \
	if(!s.empty()) oss << OSS_Clean(#s) << " = " << s <<std::endl
	_OSS(_info.header.app_name);
	_OSS(_info.header.app_version);

#undef _OSS
	std::string s(oss.str());
	return s;
}

std::shared_ptr<CAbstractItem> CInArchive::GetItem(size_t index)
{
	using std::make_shared;
	if (index >= _info.files.size())
		return make_shared<CEmbeddedItem>(_embedded.GetItem(index - _info.files.size()));
	else
	{
		const setup::file_entry& f = _info.files.at(index);
		if (f.location == (uint32_t)(-1))
			return make_shared<CItem>(CItem(f, setup::data_entry(),_hasCollision));
		else
			return make_shared<CItem>(CItem(f, _info.data_entries.at(f.location), _hasCollision));
	}
}

CInArchiveReadProgressQueryHelper CInArchive::GetReadProgressQueryHelper() const
{
	return CInArchiveReadProgressQueryHelper(this);
}

bool CInArchive::CheckCollideLocations(UInt32 index, UInt32& firstAppearance)
{
	if (_collisionLocations.empty())
		return false;
	if (index >= _info.files.size() || index < 0)
		return false;
	auto find_result = _collisionLocations.find(index);
	if (find_result == _collisionLocations.end())
	{
		firstAppearance = (UInt32)(-1);
		return false;
	}
	else
	{
		firstAppearance = static_cast<UInt32>(find_result->second);
		return true;
	}
}

HRESULT CInArchive::ExtractItem(UInt32 indice, bool testMode, InteropFileStreamProgressWriter* outStream, std::string* savedBuffer)
{
	size_t indice64 = indice;
	if (indice64 >= _info.files.size())
		return _embedded.ExtractItem(indice64 - _info.files.size(), testMode, outStream);
	if (!_extractInitialized)
		PrepareExtraction();
	setup::file_entry& file_info = _info.files[indice64];
	if (file_info.location >= _info.data_entries.size())
	{
		if (file_info.location == (uint32_t)(-1))
			return S_OK;
		else
			return S_FALSE;
	}
	setup::data_entry& data_info = _info.data_entries[file_info.location];
	stream::chunk& chunk = data_info.chunk;
	chunk_reader* chunk_source = OpenChunk(chunk);
	crypto::checksum checksum;
	if(data_info.file.offset>(uint64_t)chunk_source->TotalRead())
	{
		util::discard(*chunk_source, data_info.file.offset - chunk_source->TotalRead());
	}
	else if (data_info.file.offset < (uint64_t)chunk_source->TotalRead())
	{
		chunk_source=ResetChunkReader(chunk);
		util::discard(*chunk_source, data_info.file.offset - chunk_source->TotalRead());
	}
	unique_ptr<istream> file_source = stream::file_reader::get(*chunk_source, data_info.file, &checksum);
	constexpr size_t buffer_size = 16 * 1024;
	char* p_buffer = new char[buffer_size];
	uint64_t total_output = 0;
	while (!file_source->eof())
	{
		file_source->read(p_buffer, buffer_size);
		std::streamsize n = file_source->gcount();
		if (n == (std::streamsize)-1 && file_source->eof())
		{
			n = 0;
		}
		if (savedBuffer != nullptr)
			savedBuffer->append(p_buffer, (size_t)n);
		if (!testMode)
		{
			RINOK(outStream->Write(p_buffer, (UInt32)n));
		}
		total_output += n;
	}
	delete[] p_buffer;
	if (total_output != data_info.uncompressed_size)
	{
		log_warning << "Extracted size mismatch for file " << file_info.destination << " expected: " << data_info.uncompressed_size << " actual: " << total_output;
		return ERROR_INCORRECT_SIZE;
	}
	if (checksum != data_info.file.checksum)
	{
		log_warning << "Checksum mismatch for file " << file_info.destination;
		return ERROR_DATA_CHECKSUM_ERROR;
	}
	return S_OK;
}

bool CInArchive::OpenInfo()
{
	setup::info::entry_types entries = setup::info::entry_types::all();

	_pInStream->seekg(_offsets.header_offset);
	try {
		_info.load(*_pInStream, entries);
	}
	catch (const std::exception& e)
	{
		std::cout << "Stream error while parsing setup headers!\n";
		std::cout << " |-- detected setup version: " << _info.version << '\n';
		std::cout << " --- error reason: " << e.what();
		return false;
	}
	return true;
}

void CInArchive::PrepareExtraction()
{
	if (_offsets.data_offset) {
		_pSliceReader.reset(new stream::slice_reader(_pInStream.get(), _offsets.data_offset));
		_extractInitialized = true;
	}
	else {
		//TODO: implement multi-part setup files.
		throw NotImplemented(__FUNCSIG__);
		//fs::path dir = installer.parent_path();
		//std::string basename = util::as_string(installer.stem());
		//std::string basename2 = info.header.base_filename;
		//// Prevent access to unexpected files
		//std::replace(basename2.begin(), basename2.end(), '/', '_');
		//std::replace(basename2.begin(), basename2.end(), '\\', '_');
		//// Older Inno Setup versions used the basename stored in the headers, change our default accordingly
		//if (info.version < INNO_VERSION(4, 1, 7) && !basename2.empty()) {
		//	std::swap(basename2, basename);
		//}
		//_pSliceReader.reset(new stream::slice_reader(dir, basename, basename2, info.header.slices_per_disk));
	}
}

void CInArchive::FindCollideLocation()
{
	//check for collision, both locations and file names
	std::unordered_map<size_t, size_t> unique_locations; // map of unique locations to their first appearance
	std::unordered_set<std::string> unique_paths;
	for (size_t i = 0; i < _info.files.size(); i++)
	{
		auto insert_result = unique_locations.insert(std::make_pair(_info.files[i].location, i));
		if (insert_result.second == false)
		{
			_collisionLocations.emplace(i, insert_result.first->second);
		}
		auto path_result = unique_paths.insert(_info.files[i].destination);
		if (path_result.second == false)
		{
			_hasCollision = true;
		}
	}
}

CInArchive::chunk_reader* CInArchive::OpenChunk(stream::chunk& chunk)
{
	if (chunk != _chunk)
	{
		_pChunkReader = stream::chunk_reader::get(*_pSliceReader, chunk, password);
		_chunk = chunk;
	}
	return _pChunkReader.get();
}

CInArchive::chunk_reader* CInArchive::ResetChunkReader(stream::chunk& chunk)
{
	_pChunkReader = stream::chunk_reader::get(*_pSliceReader, chunk, password);
	return _pChunkReader.get();
}

CEmbeddedFiles::CEmbeddedFiles(setup::info& info, bool hasCollision, FILETIME time)
	: _pinfo(&info), _hasCollision(hasCollision)
{
	_time = NWindows::NTime::FileTime_To_UnixTime64(time);
}

size_t CEmbeddedFiles::GetNumberOfItems() const
{
	// add more files
	size_t additionalItems = 1; // inno setup script
	additionalItems += (_pinfo->header.license_text.empty() ? 0 : 1); // license file
	additionalItems += _pinfo->languages.size(); // language files
	for (const auto& lang : _pinfo->languages)
	{
		additionalItems += (lang.license_text.empty() ? 0 : 1); // language specific license file
	}
	additionalItems += _pinfo->wizard_images.size(); // wizard images
	additionalItems += _pinfo->wizard_images_small.size(); // wizard images small
	return additionalItems;
}

CEmbeddedItem CEmbeddedFiles::GetItem(size_t index) const
{
	if (index == 0)
	{
		return GenerateInstallScript();
	}
	index -= 1;

	if (index == 0)
	{
		return GenerateLicense();
	}
	index -= 1;
	
	if (index < _pinfo->languages.size())
	{
		return GenerateLanguageFile(index);
	}
	index -= _pinfo->languages.size();
	
	{
		size_t i = 0;
		while (index >= 0 && i < _pinfo->languages.size())
		{
			if (!_pinfo->languages[i].license_text.empty())
			{
				if (index == 0)
				{
					return GenerateLanguageLicense(i);
				}
				index -= 1;
			}
			i++;
		}
	}

	if (index >= 0 && index < _pinfo->wizard_images.size())
	{
		return GenerateWizardImage(index);
	}
	index -= _pinfo->wizard_images.size();

	if (index >= 0 && index < _pinfo->wizard_images_small.size())
	{
		return GenerateWizardImageSmall(index);
	}
	index -= _pinfo->wizard_images_small.size();
	
	return CEmbeddedItem();
}

HRESULT CEmbeddedFiles::ExtractItem(size_t indice, bool testMode, InteropFileStreamProgressWriter* outStream) const
{
	bool generated = false;
	std::string data;
	size_t dataSize=0;
	if (!generated && indice == 0)
	{
		dataSize = GenerateInstallScriptContent(data);
		generated = true;
	}
	indice -= 1;
	
	if (!generated && indice == 0)
	{
		dataSize = GenerateLicenseContent(data);
		generated = true;
	}
	indice -= 1;
	
	if (!generated && indice < _pinfo->languages.size())
	{
		dataSize = GenerateLanguageFileContent(indice, data);
		generated = true;
	}
	
	if (!generated)
	{
		indice -= _pinfo->languages.size();
		size_t i = 0;
		while (indice >= 0 && i < _pinfo->languages.size())
		{
			if (!_pinfo->languages[i].license_text.empty())
			{
				if (indice == 0)
				{
					dataSize = GenerateLanguageLicenseContent(i, data);
					generated = true;
					break;
				}
				indice -= 1;
			}
			i++;
		}
	}

	if (!generated && indice < _pinfo->wizard_images.size())
	{
		dataSize = GenerateWizardImageContent(indice, data);
		generated = true;
	}
	indice -= _pinfo->wizard_images.size();
	
	if (!generated && indice < _pinfo->wizard_images_small.size())
	{
		dataSize = GenerateWizardImageSmallContent(indice, data);
		generated = true;
	}
	indice -= _pinfo->wizard_images_small.size();
	
	if (!testMode && generated)
	{
		RINOK(outStream->Write(data.data(), dataSize));
	}
	return S_OK;
}

CEmbeddedItem CEmbeddedFiles::GenerateInstallScript() const
{
	return CEmbeddedItem(
		"install_script.iss",
		0,
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateInstallScriptContent(std::string & outData) const
{
	using std::ostringstream;
	using std::endl;
	ostringstream oss;
	oss << "; InnoSetupVersion=" << _pinfo->version << endl;
	oss << endl;

	{
		oss << "[Setup]" << endl;
#		define _OSSL(s) if (!s.empty()) oss << OSS_Clean(#s) << "=" << s << "\n"
#		define _OSSL2(s,n) if (!s.empty()) oss << n << "=" << s << "\n"
		_OSSL(_pinfo->header.app_name);
		_OSSL(_pinfo->header.app_versioned_name);
		_OSSL(_pinfo->header.app_id);
		_OSSL(_pinfo->header.app_version);
		_OSSL(_pinfo->header.app_publisher);
		_OSSL(_pinfo->header.app_publisher_url);
		_OSSL(_pinfo->header.app_support_phone);
		_OSSL(_pinfo->header.app_support_url);
		_OSSL(_pinfo->header.app_updates_url);
		_OSSL(_pinfo->header.app_mutex);
		_OSSL(_pinfo->header.app_comments);
		_OSSL(_pinfo->header.app_modify_path);
		if (_pinfo->header.options & setup::header::CreateAppDir)
		{
			_OSSL(_pinfo->header.default_dir_name);
		}
		else
		{
			oss << "CreateAppDir=no\n";
		}
		if (_pinfo->header.default_group_name != "(Default)")
		{
			_OSSL(_pinfo->header.default_group_name);
		}
		_OSSL2(_pinfo->header.uninstall_icon, "UninstallDisplayIcon");
		_OSSL2(_pinfo->header.uninstall_icon_name,"UninstallDisplayName");
		
#		undef _OSSL
#		undef _OSSL2
		oss << "; uncompleted" << endl;
	}

	// Types
	if(!_pinfo->types.empty())
	{
		oss << "[Types]" << endl;  // optional
		for (const auto& i : _pinfo->types)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"';
			_OSS(i.description);
			_OSS(i.check);
#			undef _OSS
			if (i.custom_type)
			{
				oss << "; Flags: iscustom";
			}
			oss << endl;
		}
		oss << endl;
	}
	// Components
	if(!_pinfo->components.empty())
	{
		oss << "[Components]" << endl; // optional
		for (const auto& i : _pinfo->components)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s) << ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			_OSS(i.description);
			_OSS(i.types);
			_OSSN(i.extra_disk_pace_required);
			_OSS(i.check);
#			undef _OSS
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Tasks
	if (!_pinfo->tasks.empty())
	{
		oss << "[Tasks]" << endl; // optional
		for (const auto& i : _pinfo->tasks)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": " << s 
			_OSS(i.description);
			_OSS(i.group_description);
			_OSS2(i.components);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Dir
	if(!_pinfo->directories.empty())
	{
		oss << "[Dir]" << endl; // optional
		for (const auto& i : _pinfo->directories)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.permissions);
			//_OSS(i.attributes);
			_OSS(i.check);
#			undef _OSS
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// File
	{
		oss << "[File]" << endl;
		for (const auto& i : _pinfo->files)
		{
			oss << "Source: \"" << (i.source.empty() ? i.destination : i.source) << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			size_t sep=i.destination.find_last_of('\\');
			if (sep != std::string::npos)
			{
				std::string_view destDir = std::string_view(i.destination.data(),sep);
				std::string_view destName = std::string_view(i.destination.data()+sep+1);
				_OSS2(destDir, "DestDir");
				_OSS2(destName, "DestName");
			}
			else
			{
				_OSS2(i.destination,"DestDir");
			}
			_OSSN(i.external_size);
			//_OSS(i.attributes);
			//_OSS(i.permission);
			_OSS2(i.install_font_name,"FontInstall");
			_OSS(i.strong_assembly_name);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Icons
	{
		oss << "[Icons]" << endl;
		for (const auto& i : _pinfo->icons)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			_OSS(i.filename);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			//_OSS(i.hotkey);
			_OSS(i.comment);
			_OSS2(i.icon_file,"IconFilename");
			_OSSN(i.icon_index);
			_OSS(i.app_user_model_id);
			_OSS(i.app_user_model_toast_activator_clsid);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// INI
	if(!_pinfo->ini_entries.empty())
	{
		oss << "[INI]" << endl; // optional
		for (const auto& i : _pinfo->ini_entries)
		{
			oss << "Filename: \"" << i.inifile << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.section);
			_OSS(i.key);
			_OSS2(i.value,"String");
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// InstallDelete
	if(!_pinfo->delete_entries.empty())
	{
		oss << "[InstallDelete]" << endl; // optional
		for (const auto& i : _pinfo->delete_entries)
		{
			using enum_type = std::remove_reference_t<decltype(i)>::target_type;
			static_assert(enum_names<enum_type>::named == 1);
			oss << "Type: " << Flag_Clean(enum_names<enum_type>::names[i.type]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.name);
			_OSS(i.check);
#			undef _OSS
			oss << endl;
		}
		oss << endl;
	}
	// Languages
	{
		oss << "[Languages]" << endl;
		for (const auto& i : _pinfo->languages)
		{
			oss << "Name: \"" << i.name << '"';
			oss << "; MessagesFile: \"embedded\\" << i.name << ".isl\"";
			if (!i.license_text.empty())
			{
				oss << "; LicenseFile: \"embedded\\" << i.name << ".rtf\"";
			}
			oss << endl;
		}
		oss << endl;
	}
	// CustomMessages
	if (!_pinfo->messages.empty())
	{
		oss << "[CustomMessages]" << endl; // optional
		for (const auto& i : _pinfo->messages)
		{
			using namespace std::literals;
			if (i.language != -1)
			{
				oss << _pinfo->languages[i.language].name << '.';
			}
			std::string unescaped_value = ReplaceString(i.value, "\n"s, "\\n"s);
			unescaped_value = ReplaceString(unescaped_value, "\r"s, "\\r"s);
			oss << i.name << '=' << unescaped_value << endl;
		}
		oss << endl;
	}
	// Registry
	if(!_pinfo->registry_entries.empty())
	{
		oss << "[Registry]" << endl;
		for (const auto& i : _pinfo->registry_entries)
		{
			using hive_name = std::remove_reference_t<decltype(i)>::hive_name;
			using value_type = std::remove_reference_t<decltype(i)>::value_type;
			static_assert(enum_names<hive_name>::named == 1);
			static_assert(enum_names<value_type>::named == 1);
			oss << "Root: " << Flag_Clean(enum_names<hive_name>::names[i.hive]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS2(i.key, "Subkey");
			oss << "; ValueType: " << Flag_Clean(enum_names<value_type>::names[i.type]);
			_OSS2(i.name, "ValueName");
			_OSS2(i.value,"ValueData");
			_OSS(i.permissions);
			_OSS(i.check);
#			undef _OSS			
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Run
	if (!_pinfo->run_entries.empty())
	{
		oss << "[Run]" << endl; // optional
		for (const auto& i : _pinfo->run_entries)
		{
			oss << "Filename: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.description);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			_OSS2(i.status_message, "StatusMsg");
			_OSS(i.run_once_id);
			_OSS(i.verb);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// UninstallDelete
	if (!_pinfo->uninstall_delete_entries.empty())
	{
		oss << "[UninstallDelete]" << endl; // optional
		for (const auto& i : _pinfo->uninstall_delete_entries)
		{
			using enum_type = std::remove_reference_t<decltype(i)>::target_type;
			static_assert(enum_names<enum_type>::named == 1);
			oss << "Type: " << Flag_Clean(enum_names<enum_type>::names[i.type]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.name);
			_OSS(i.check);
#			undef _OSS
			oss << endl;
		}
		oss << endl;
	}
	// Uninstall Run
	if (!_pinfo->uninstall_run_entries.empty())
	{
		oss << "[UninstallRun]" << endl; // optional
		for (const auto& i : _pinfo->uninstall_run_entries)
		{
			oss << "Filename: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.description);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			_OSS2(i.status_message, "StatusMsg");
			_OSS(i.run_once_id);
			_OSS(i.verb);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}

	outData = oss.str();
	return outData.length();
}

CEmbeddedItem CEmbeddedFiles::GenerateLicense() const
{
	return CEmbeddedItem(
		"License.rtf",
		_pinfo->header.license_text.size(),
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateLicenseContent(std::string & outData) const
{
	outData = _pinfo->header.license_text;
	return _pinfo->header.license_text.length();
}

CEmbeddedItem CEmbeddedFiles::GenerateLanguageFile(size_t langIndex) const
{
	const auto& lang = _pinfo->languages[langIndex];
	return CEmbeddedItem(
		lang.name+".isl",
		0,
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateLanguageFileContent(size_t langIndex, std::string& outData) const
{
	using std::ostringstream;
	using std::endl;
	ostringstream oss;
	oss << "[LangOptions]" << endl;
	const auto& lang = _pinfo->languages[langIndex];
#	define _OSSL(s) if (!s.empty()) oss << OSS_Clean(#s) << "=" << s << "\n"
#	define _OSSLN(s) oss << OSS_Clean(#s) << "=" << s << "\n"
#	define _OSSL2(s,n) if (!s.empty()) oss << n << "=" << s << "\n"
#	define _OSSLN2(s,n) oss << n << "=" << s << "\n"
	_OSSL(lang.language_name);
	// use hex instead "${hex of language id}"?
	_OSSLN(lang.language_id);
	_OSSLN2(lang.codepage,"LanguageCodePage");
	_OSSL2(lang.dialog_font,"DialogFontName");
	_OSSL2(lang.title_font,"TitleFontName");
	_OSSL2(lang.welcome_font,"WelcomeFontName");
	_OSSL2(lang.copyright_font, "CopyrightFontName");
	_OSSLN(lang.dialog_font_size);
	_OSSLN(lang.title_font_size);
	_OSSLN(lang.welcome_font_size);
	_OSSLN(lang.copyright_font_size);
	_OSSLN(lang.right_to_left);
	_OSSL(lang.data);
#	undef _OSSL
#	undef _OSSLN
#	undef _OSSL2
#	undef _OSSLN2
	outData = oss.str();
	return outData.size();
}

CEmbeddedItem CEmbeddedFiles::GenerateLanguageLicense(size_t langIndex) const
{
	const auto& lang = _pinfo->languages[langIndex];
	return CEmbeddedItem(
		lang.name + ".rtf",
		lang.license_text.length(),
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateLanguageLicenseContent(size_t langIndex, std::string& outData) const
{
	const auto& lang = _pinfo->languages[langIndex];
	outData = lang.license_text;
	return lang.license_text.length();
}

CEmbeddedItem CEmbeddedFiles::GenerateWizardImage(size_t imageIndex) const
{
	return CEmbeddedItem(
		"WizardImage" + std::to_string(imageIndex) + ".bmp",
		_pinfo->wizard_images[imageIndex].size(),
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateWizardImageContent(size_t imageIndex, std::string& outData) const
{
	outData = _pinfo->wizard_images[imageIndex];
	return outData.size();
}

CEmbeddedItem CEmbeddedFiles::GenerateWizardImageSmall(size_t imageIndex) const
{
	return CEmbeddedItem(
		"WizardImageSmall" + std::to_string(imageIndex) + ".bmp",
		_pinfo->wizard_images_small[imageIndex].size(),
		GetMethod(),
		_time
	);
}

size_t CEmbeddedFiles::GenerateWizardImageSmallContent(size_t imageIndex, std::string& outData) const
{
	outData = _pinfo->wizard_images_small[imageIndex];
	return outData.size();
}

const char* const CEmbeddedFiles::GetMethod() const
{
	switch (_pinfo->header.compression)
	{
	case stream::compression_method::Stored:
		return kMethods[0];
	case stream::compression_method::Zlib:
		return kMethods[1];
	case stream::compression_method::BZip2:
		return kMethods[2];
	case stream::compression_method::LZMA1:
		return kMethods[3];
	case stream::compression_method::LZMA2:
		return kMethods[4];
	case stream::compression_method::UnknownCompression:
	default:
		return kMethods[5];
	}
}

CInArchiveReadProgressQueryHelper::CInArchiveReadProgressQueryHelper(const CInArchive* archive)
{
	_archive = archive;
}

UInt64 CInArchiveReadProgressQueryHelper::TotalRead() const
{
	if (_archive != nullptr)
		return _archive->_pInteropStream->TotalRead();
	else
		return 0;
}

}
}