// Origin: innoextract src/stream/block.cpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: blocks are read and decompressed into
// memory with 7-Zip decoders instead of boost::iostreams filters.
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

#include "../stream/block.hpp"

#include <cstring>
#include <string>
#include <istream>
#include <sstream>

#include "../../InnoBufferDecoder.h"

#include "../crypto/crc32.hpp"
#include "../setup/version.hpp"
#include "../util/endian.hpp"
#include "../util/enum.hpp"
#include "../util/load.hpp"
#include "../util/log.hpp"
#include "../util/math.hpp"

namespace stream {

namespace {

enum block_compression {
	Stored,
	Zlib,
	LZMA1,
};

/*!
 * Reads a block of 4096-byte chunks where each chunk is preceeded by a CRC32 checksum.
 * The last chunk can be shorter than 4096 bytes.
 *
 * block_error is thrown if a chunk checksum is wrong, if the data ends early or if there
 * is trailing data: 0 < (total size % (4096 + 4)) < 5
 */
void read_block_chunks(std::istream & base, boost::uint32_t stored_size, std::string & data) {
	
	data.clear();
	data.reserve(stored_size);
	
	char buffer[4096];
	while(stored_size) {
		
		if(stored_size < 5) {
			throw block_error("unexpected block end");
		}
		
		char temp[sizeof(boost::uint32_t)];
		base.read(temp, std::streamsize(sizeof(temp)));
		if(size_t(base.gcount()) != sizeof(temp)) {
			throw block_error("unexpected block end");
		}
		boost::uint32_t block_crc32 = util::little_endian::load<boost::uint32_t>(temp);
		stored_size -= boost::uint32_t(sizeof(temp));
		
		size_t length = std::min(size_t(stored_size), sizeof(buffer));
		base.read(buffer, std::streamsize(length));
		if(size_t(base.gcount()) != length) {
			throw block_error("unexpected block end");
		}
		stored_size -= boost::uint32_t(length);
		
		crypto::crc32 actual;
		actual.init();
		actual.update(buffer, length);
		if(actual.finalize() != block_crc32) {
			throw block_error("block CRC32 mismatch");
		}
		
		data.append(buffer, length);
	}
}

} // anonymous namespace

} // namespace stream

NAMED_ENUM(stream::block_compression)

NAMES(stream::block_compression, "Compression",
	"stored",
	"zlib",
	"lzma1",
)

namespace stream {

block_reader::pointer block_reader::get(std::istream & base, const setup::version & version, size_t* block_size) {
	
	USE_ENUM_NAMES(block_compression)
	
	boost::uint32_t expected_checksum = util::load<boost::uint32_t>(base);
	crypto::crc32 actual_checksum;
	actual_checksum.init();
	
	boost::uint32_t stored_size;
	block_compression compression;
	
	if(version >= INNO_VERSION(4, 0, 9)) {
		
		stored_size = actual_checksum.load<boost::uint32_t>(base);
		boost::uint8_t compressed = actual_checksum.load<boost::uint8_t>(base);
		
		compression = compressed ? (version >= INNO_VERSION(4, 1, 6) ? LZMA1 : Zlib) : Stored;
		
	} else {
		
		boost::uint32_t compressed_size = actual_checksum.load<boost::uint32_t>(base);
		boost::uint32_t uncompressed_size = actual_checksum.load<boost::uint32_t>(base);
		
		if(compressed_size == boost::uint32_t(-1)) {
			stored_size = uncompressed_size, compression = Stored;
		} else {
			stored_size = compressed_size, compression = Zlib;
		}
		
		// Add the size of a CRC32 checksum for each 4KiB subblock.
		stored_size += boost::uint32_t(util::ceildiv<boost::uint64_t>(stored_size, 4096) * 4);
	}
	
	if(actual_checksum.finalize() != expected_checksum) {
		throw block_error("block header CRC32 mismatch");
	}
	
	debug("[block] size: " << stored_size << "  compression: " << compression);

	if(block_size != nullptr) {
        *block_size = stored_size;
    }
	
	std::string stored;
	read_block_chunks(base, stored_size, stored);
	
	const Byte * stored_data = reinterpret_cast<const Byte *>(stored.data());
	util::unique_ptr<std::istringstream>::type result(new std::istringstream);
	switch(compression) {
		case Stored: {
			result->str(stored);
			break;
		}
		case Zlib: {
			std::string decoded;
			if(!NArchive::NInno::DecodeZlibBuffer(stored_data, stored.size(), decoded)) {
				throw block_error("zlib decompression error");
			}
			result->str(decoded);
			break;
		}
		case LZMA1: {
			std::string decoded;
			if(!NArchive::NInno::DecodeLzma1Buffer(stored_data, stored.size(), decoded)) {
				throw block_error("lzma decompression error");
			}
			result->str(decoded);
			break;
		}
	}
	
	result->exceptions(std::ios_base::badbit | std::ios_base::failbit);
	
	return pointer(result.release());
}

} // namespace stream
