#include "StdAfx.h"

#include "CPP/Windows/TimeUtils.h"

#include "InnoItem.h"
#include "InnoUtils.h"

#include <string_view>
#include <vector>

namespace NArchive {
namespace NInno {

static const char* const kCompressionMethods[] =
{
	"Copy"
  , "Deflate"
  , "BZip2"
  , "LZMA"
  , "LZMA2"
  , "Unknown"
};

static const char* const kFilterMethods[] = {
	"NoFilter",
	"IF4108",
	"IF5200",
	"IF5309",
	"ZlibFilter"
};

// Splits at every space, like std::views::split(' ') (C++20).
static std::vector<std::string_view> SplitAtSpaces(std::string_view s)
{
	std::vector<std::string_view> words;
	size_t start = 0;
	for (;;)
	{
		const size_t pos = s.find(' ', start);
		if (pos == std::string_view::npos)
		{
			words.push_back(s.substr(start));
			return words;
		}
		words.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
}

FileArch CItem::GetArch(const setup::file_entry& file)
{
	state3 bits32, bits64, arm;
	if (file.options & setup::file_entry::Bits32)
		bits32 = true;
	if (file.options & setup::file_entry::Bits64)
		bits64 = true;
	
	auto split = SplitAtSpaces(file.check);
	bool reverse = false;
	for (auto&& word : split)
	{
		if (word == "not")
		{
			reverse = true;
			continue;
		}
		else if (word == "Is64BitInstallMode")
		{
			if (reverse)
				bits64 = false;
			else
				bits64 = true;
		}
		else if (word == "IsWin64")
		{
			if (reverse)
				bits64 = false;
			else
				bits64 = true;
		}
		else if (word == "IsARM64")
		{
			if (reverse)
				arm = false;
			else
				arm = true;
		}
		if (!word.empty())
			reverse = false;
	}
	if(arm)
		return FileArch::arm64;
	if (bits64)
		return FileArch::x64;
	if (bits32)
		return FileArch::x86;
	return FileArch::unknown;
}

std::string CItem::GetPath()
{
	using namespace std::literals;
	if (_file.type == setup::file_entry::file_type::UninstExe)
		return "unins___.exe"s;
	if (_hasCollisionInArchive)
	{
		switch (GetArch(_file))
		{
		case FileArch::x86:
			return "{x86}\\" + _file.destination;
		case FileArch::x64:
			return "{x64}\\" + _file.destination;
		case FileArch::arm64:
			return "{arm64}\\" + _file.destination;
		case FileArch::unknown:
			return _file.destination;
		}
	}
	return _file.destination;
}

UInt64 CItem::GetSize()
{
	if (_file.type == setup::file_entry::file_type::UninstExe)
		return (UInt64)0;
	return _data.uncompressed_size;
}

UInt64 CItem::GetOffset()
{
	if (_file.type == setup::file_entry::file_type::UninstExe)
		return (UInt64)0;
	return _data.file.offset;
}

UInt32 CItem::GetAttrib()
{
	return _file.attributes;
}

UInt32 CItem::GetChecksum()
{
	return _data.file.checksum.crc32;
}

bool CItem::GetEnryption()
{
	return _data.chunk.encryption!=stream::encryption_method::Plaintext;
}

std::string_view CItem::GetMethod()
{
	std::string s1, s2;
	switch (_data.chunk.compression)
	{
		case stream::compression_method::Stored:
			break;
		case stream::compression_method::Zlib:
			s1 = kCompressionMethods[1];
			break;
		case stream::compression_method::BZip2:
			s1 = kCompressionMethods[2];
			break;
		case stream::compression_method::LZMA1:
			s1 = kCompressionMethods[3];
			break;
		case stream::compression_method::LZMA2:
			s1 = kCompressionMethods[4];
			break;
		case stream::compression_method::UnknownCompression:
		default:
			s1 = kCompressionMethods[5];
			break;
	}
	switch (_data.file.filter)
	{
	case stream::compression_filter::NoFilter:
	default:
		break;
	case stream::compression_filter::InstructionFilter4108:
		s2 = kFilterMethods[1];
		break;
	case stream::compression_filter::InstructionFilter5200:
		s2 = kFilterMethods[2];
		break;
	case stream::compression_filter::InstructionFilter5309:
		s2 = kFilterMethods[3];
		break;
	case stream::compression_filter::ZlibFilter:
		s2 = kFilterMethods[4];
		break;
	}
	return (s1.empty() || s2.empty() ? s1 + s2 : s1 + ":" + s2);
}

std::string_view CItem::GetComment()
{
	return _file.check;
}

FILETIME CItem::GetModifiedTime()
{
	FILETIME ft{};
	if (_file.type == setup::file_entry::file_type::UninstExe)
		return ft;
	NWindows::NTime::UnixTime64_To_FileTime(_data.timestamp, ft);
	return ft;
}

FILETIME CEmbeddedItem::GetModifiedTime()
{
	FILETIME ft{};
	NWindows::NTime::UnixTime64_To_FileTime(_timestamp, ft);
	return ft;
}

}
}