// Origin: innoextract src/stream/block.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin.
#include "../minihead.hpp"
/*
 * Copyright (C) 2011-2014 Daniel Scharrer
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
 * Wrapper to read, checksum and decompress header blocks.
 *
 * Thse blocks are used to store the setup headers (\ref setup).
 */
#ifndef INNOEXTRACT_STREAM_BLOCK_HPP
#define INNOEXTRACT_STREAM_BLOCK_HPP

#include <ios>
#include <istream>
#include <string>

#include "../util/unique_ptr.hpp"

namespace setup { struct version; }

namespace stream {

//! Error thrown by \ref block_reader::get or the returned stream if there was a problem.
struct block_error : public std::ios_base::failure {
	
	explicit block_error(const std::string & msg) : std::ios_base::failure(msg) { }
	
};

/*!
 * Wrapper to read compressed and checksumed block of data used to store setup headers.
 *
 * The decompressed headers are parsed in \ref setup::info, which also uses this class.
 */
class block_reader {
	
public:
	
	typedef std::istream                 type;
	typedef util::unique_ptr<type>::type pointer;
	
	/*!
	 * Wrap an input stream to read and decompress setup header blocks.
	 *
	 * Only one wrapper can be used at the same time for each \c base.
	 *
	 * \param base     The input stream for the main setup files.
	 *                 It must already be positioned at start of the block stream.
	 *                 The first block stream starts directly after the \ref setup::version
	 *                 identifier whose position is given by
	 *                 \ref loader::offsets::header_offset.
	 *                 A second block stream directly follows the first one and contains
	 *                 the \ref setup::data_entry "data entries".
	 * \param version  The version of the setup data.
	 * \param block_size Receives the stored size of the block, including the chunk checksums.
	 * \param key      Key and base nonce for blocks encrypted with XChaCha20
	 *                 (Inno Setup 6.5.0 and newer with full encryption), or an empty string.
	 * \param first_slice Value that Inno Setup XORs into the nonce for this block:
	 *                 -2 for the first block and -3 for the second one.
	 *
	 * \throws block_error if the block stream header checksum was invalid,
	 *                     or if the block compression is not supported by this build.
	 *
	 * \return a pointer to a non-seekable input stream for the uncompressed headers.
	 *         Reading from this stream may throw a \ref block_error if a block checksum
	 *         was invalid.
	 */
	static pointer get(std::istream & base, const setup::version & version, size_t* block_size = nullptr,
	                   const std::string & key = std::string(), boost::uint32_t first_slice = 0);
	
};

} // namespace stream

#endif // INNOEXTRACT_STREAM_BLOCK_HPP
