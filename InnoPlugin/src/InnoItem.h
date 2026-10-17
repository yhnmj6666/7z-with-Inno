#pragma once

#ifndef __INNO_ITEM_H
#define __INNO_ITEM_H

#include "CPP/Common/StringConvert.h"
#include "CPP/Common/UTFConvert.h"

#include "inno/setup/data.hpp"
#include "inno/setup/file.hpp"

namespace NArchive {
namespace NInno {

enum class FileArch
{
	unknown=0,
	x86,
	x64,
	arm64
};

class CAbstractItem
{
public:
	virtual std::string GetPath() = 0;
	virtual UInt64 GetSize() = 0;
	virtual UInt64 GetOffset() = 0;
	virtual UInt32 GetAttrib() = 0;
	virtual UInt32 GetChecksum() = 0;
	virtual bool GetEnryption() = 0;
	virtual std::string_view GetMethod() = 0;
	virtual std::string_view GetComment() = 0;
	virtual FILETIME GetModifiedTime() = 0;
};

class CItem : public CAbstractItem
{
	const setup::file_entry& _file;
	const setup::data_entry& _data;
	bool _hasCollisionInArchive;
public:
	CItem(const setup::file_entry& f, const setup::data_entry& d, bool hasCollisionInArchive) 
		: _file(f), _data(d), _hasCollisionInArchive(hasCollisionInArchive) {}
	static FileArch GetArch(const setup::file_entry& file);
	virtual std::string GetPath() override;
	virtual UInt64 GetSize() override;
	virtual UInt64 GetOffset() override;
	virtual UInt32 GetAttrib() override;
	virtual UInt32 GetChecksum() override;
	virtual bool GetEnryption() override;
	virtual std::string_view GetMethod() override;
	virtual std::string_view GetComment() override;
	virtual FILETIME GetModifiedTime() override;
};

class CEmbeddedItem : public CAbstractItem
{
public:
	virtual std::string GetPath() override { return "embedded\\" + _name; }
	virtual UInt64 GetSize() override { return _size; }
	virtual UInt64 GetOffset() override { return 0; }
	virtual UInt32 GetAttrib() override { return 0; }
	virtual UInt32 GetChecksum() override { return 0; }
	virtual bool GetEnryption() override { return false; }
	virtual std::string_view GetMethod() override { return _method; }
	virtual std::string_view GetComment() override { return "Auto generated"; }
	virtual FILETIME GetModifiedTime() override;
	CEmbeddedItem() = default;
	CEmbeddedItem(std::string name, size_t size, std::string method, int64_t timestamp)
		:_name(name), _size(size), _method(method), _timestamp(timestamp) {}
private:
	std::string _name;
	size_t _size;
	std::string _method;
	int64_t _timestamp;
	//std::shared_ptr<void*> _data;
};

}
}
#endif