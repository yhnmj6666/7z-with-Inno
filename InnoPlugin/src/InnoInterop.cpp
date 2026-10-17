#include "StdAfx.h"

#include "CPP/7zip/Common/StreamObjects.h"
#include "CPP/7zip/Common/StreamUtils.h"

#include "InnoInterop.h"
#include "InnoIn.h"

namespace NArchive {
namespace NInno {

using std::streambuf;

streambuf::pos_type InteropStream::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
{
	if (std::ios_base::out == which)
		return pos_type(off_type(-1));
	UInt64 offset = (UInt64)(-1);
	HRESULT result = S_FALSE;
	switch (dir)
	{
	case std::ios_base::beg:
		result = _stream->Seek(off, STREAM_SEEK_SET, &offset);
		break;
	case std::ios_base::cur:
		result = _stream->Seek(off, STREAM_SEEK_CUR, &offset);
		break;
	case std::ios_base::end:
		result = _stream->Seek(off, STREAM_SEEK_END, &offset);
		break;
	default:
		break;
	}
	if (S_OK != result)
	{
		return pos_type(off_type(-1));
	}
	return pos_type(off_type(offset));
}

streambuf::pos_type InteropStream::seekpos(pos_type pos, std::ios_base::openmode which)
{
	return seekoff(pos, std::ios_base::beg, which);
}

std::streamsize InteropStream::showmanyc()
{
	return std::streamsize(0);
}

streambuf::int_type InteropStream::underflow()
{
	char res;
	UInt32 processedSize;
	_stream->Read(&res, sizeof(res), &processedSize);
	if (0 == processedSize)
	{
		return traits_type::eof();
	}
	_totalRead += processedSize;
	_stream->Seek(-1, STREAM_SEEK_CUR, NULL);
	return traits_type::to_int_type(res);
}

streambuf::int_type InteropStream::uflow()
{
	char res;
	UInt32 processedSize;
	_stream->Read(&res, sizeof(res), &processedSize);
	if (0 == processedSize)
	{
		return traits_type::eof();
	}
	_totalRead += processedSize;
	return traits_type::to_int_type(res);
}

std::streamsize InteropStream::xsgetn(char_type* s, std::streamsize count)
{
	if (count <= 0)
		return 0;
	size_t processedSize = (size_t)count;
	// A read error ends the data like the end of the stream.
	ReadStream(_stream, s, &processedSize);
	_totalRead += processedSize;
	return std::streamsize(processedSize);
}

InteropStream::InteropStream(IInStream* stream)
{
	_stream = stream;
	_totalRead = 0;
	setg(nullptr, nullptr, nullptr);
}

UInt64 InteropStream::TotalRead() const
{
	return _totalRead;
}

InteropStream::~InteropStream()
{
	_stream.Release();
}

InteropFileStreamProgressWriter::InteropFileStreamProgressWriter(CLocalProgress* progress, QueryHelper& archiveHelper, ISequentialOutStream* outStream)
{
	_coderSpec = new NCompress::CCopyCoder();
	_coder = _coderSpec;
	_progressSpec = progress;
	_progress = progress;
	_archive = std::make_shared<QueryHelper>(archiveHelper);
	_outStream = outStream;
}

void InteropFileStreamProgressWriter::SetOutStream(ISequentialOutStream* outStream)
{
	if (_outStream)
	{
		_outStream.Release();
	}
	_outStream = outStream;
}

HRESULT InteropFileStreamProgressWriter::Write(const char* buffer, const size_t size)
{
	CBufInStream* bisSpec = new CBufInStream();
	CMyComPtr<ISequentialInStream> bis = bisSpec;
	bisSpec->Init((const Byte*)buffer, size);
	auto result=_coder->Code(bis, _outStream, nullptr, nullptr, _progress);
	_progressSpec->InSize = _archive->TotalRead();
	_progressSpec->OutSize += _coderSpec->TotalSize;
	_progressSpec->SetCur();
	return result;
}

InteropFileStreamProgressWriter::~InteropFileStreamProgressWriter()
{
	_coder.Release();
	_progress.Release();
	_outStream.Release();
}

}
}