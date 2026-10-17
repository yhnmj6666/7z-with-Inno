#pragma once

#ifndef __INNO_INTEROP_H
#define __INNO_INTEROP_H

#include "CPP/7zip/Compress/CopyCoder.h"
#include "CPP/7zip/Common/LimitedStreams.h"
#include "CPP/7zip/Common/ProgressUtils.h"

#include <iostream>
#include <memory>
#include <streambuf>

namespace NArchive {
namespace NInno {

// InnoIn.h
class CInArchiveReadProgressQueryHelper;

using std::ios_base;
using std::shared_ptr;
using QueryHelper = CInArchiveReadProgressQueryHelper;

class InteropStream : public std::streambuf {
private:
	CMyComPtr<IInStream> _stream;
	UInt64 _totalRead;
protected:
	virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir,
		std::ios_base::openmode which = ios_base::in | ios_base::out) override;
	virtual pos_type seekpos(pos_type pos,
		std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override;

	virtual std::streamsize showmanyc() override;
	virtual int_type underflow() override;
	virtual int_type uflow() override;
	virtual std::streamsize xsgetn(char_type* s, std::streamsize count) override;

public:
	InteropStream(IInStream* stream);
	UInt64 TotalRead() const;
	virtual ~InteropStream();
};

class InteropFileStreamProgressWriter
{
private:
	shared_ptr<QueryHelper> _archive;
	NCompress::CCopyCoder* _coderSpec;
	CMyComPtr<ICompressCoder> _coder;
	CLocalProgress* _progressSpec;
	CMyComPtr<ICompressProgressInfo> _progress;
	CMyComPtr<ISequentialOutStream> _outStream;
public:
	InteropFileStreamProgressWriter(CLocalProgress* progress, QueryHelper& archiveHelper, ISequentialOutStream* outStream = nullptr);
	void SetOutStream(ISequentialOutStream* outStream);
	HRESULT Write(const char* buffer, const size_t size);
	virtual ~InteropFileStreamProgressWriter();
};

}
}

#endif