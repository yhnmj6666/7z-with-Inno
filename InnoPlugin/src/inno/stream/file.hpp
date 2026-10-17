// Origin: innoextract src/stream/file.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: the file reader is replaced by
// CInArchive::ExtractItem (src/InnoIn.cpp).
#include "../minihead.hpp"
/*
 * Copyright (C) 2011-2018 Daniel Scharrer
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
 * Information about a single file stored in a chunk.
 */
#ifndef INNOEXTRACT_STREAM_FILE_HPP
#define INNOEXTRACT_STREAM_FILE_HPP

#include "../crypto/checksum.hpp"

namespace stream {

enum compression_filter {
	NoFilter,
	InstructionFilter4108,
	InstructionFilter5200,
	InstructionFilter5309,
	ZlibFilter,
};

/*!
 * Information specifying a single file inside a compressed chunk.
 *
 * This data is stored in \ref setup::data_entry "data entries".
 */
struct file {
	
	boost::uint64_t    offset;   //!< Offset of this file within the decompressed chunk.
	boost::uint64_t    size;     //!< Pre-filter size of this file in the decompressed chunk.
	
	crypto::checksum   checksum; //!< Checksum for the file.
	
	compression_filter filter;   //!< Additional filter used before compression.
	
	bool operator<(const file & o) const;
	bool operator==(const file & o) const;
	
};

} // namespace stream

#endif // INNOEXTRACT_STREAM_FILE_HPP
