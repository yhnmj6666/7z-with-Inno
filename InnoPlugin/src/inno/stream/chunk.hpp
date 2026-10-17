// Origin: innoextract src/stream/chunk.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: the chunk reader is replaced by
// NArchive::NInno::CChunkDecoder (src/InnoDecoder.h).
#include "../minihead.hpp"
/*
 * Copyright (C) 2011-2019 Daniel Scharrer
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the author(s) be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/*!
 * \file
 *
 * Information about a compressed chunk.
 *
 * A chunk consists of one compression stream (one of \ref stream::compression_method) and
 * contains one or more \ref stream::file "files".
 * Files may also have additional filters (\ref stream::compression_filter).
 */
#ifndef INNOEXTRACT_STREAM_CHUNK_HPP
#define INNOEXTRACT_STREAM_CHUNK_HPP

#include <stddef.h>
#include <ios>
#include <string>

#include "../util/enum.hpp"

namespace stream {

//! Compression methods supported by chunks.
enum compression_method {
	Stored,
	Zlib,
	BZip2,
	LZMA1,
	LZMA2,
	UnknownCompression
};

//! Encryption methods supported by chunks.
enum encryption_method {
	Plaintext,
	ARC4_MD5,
	ARC4_SHA1,
};

/*!
 * Information specifying a compressed chunk.
 *
 * This data is stored in \ref setup::data_entry "data entries".
 */
struct chunk {
	
	boost::uint32_t first_slice;    //!< Slice where the chunk starts.
	boost::uint32_t last_slice;     //!< Slice where the chunk ends.
	
	boost::uint32_t sort_offset;
	
	boost::uint32_t offset;         //!< Offset of the compressed chunk in firstSlice.
	boost::uint64_t size;           //! Total compressed size of the chunk.
	
	compression_method compression; //!< Compression method used by the chunk.
	encryption_method encryption;   //!< Encryption method used by the chunk.
	
	bool operator<(const chunk & o) const;
	bool operator==(const chunk & o) const;
	
};

} // namespace stream

NAMED_ENUM(stream::compression_method)

NAMED_ENUM(stream::encryption_method)

#endif // INNOEXTRACT_STREAM_CHUNK_HPP
