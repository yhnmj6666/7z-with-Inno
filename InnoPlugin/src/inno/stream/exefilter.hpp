// Origin: innoextract src/stream/exefilter.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: the filters decode buffers in place
// instead of being boost::iostreams filters.
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
 * Filters for undoing transformations Inno Setup applies to stored executable
 * files to make them more compressible.
 */
#ifndef INNOEXTRACT_STREAM_EXEFILTER_HPP
#define INNOEXTRACT_STREAM_EXEFILTER_HPP

#include <stddef.h>

#include "../minihead.hpp"

namespace stream {

/*!
 * Filter to decode executable files stored by Inno Setup versions before 5.2.0.
 *
 * Essentially, it tries to change the addresses stored for x86 CALL and JMP instructions
 * to be relative to the instruction's position.
 */
class inno_exe_decoder_4108 {
	
public:
	
	inno_exe_decoder_4108() : addr(0), addr_bytes_left(0), addr_offset(5) { }
	
	//! Decode size bytes of data in place.
	void decode(boost::uint8_t * data, size_t size);
	
private:
	
	boost::uint32_t addr;
	size_t addr_bytes_left;
	boost::uint32_t addr_offset;
	
};

/*!
 * Filter to decode executable files stored by Inno Setup versions after 5.2.0.
 *
 * It operates on 64KiB chunks and tries to change the addresses stored for x86 CALL and
 * JMP instructions to be relative to the instruction's position. Instructions that span
 * chunk boundaries are ignored.
 *
 * Inno Setup 5.3.9 and later additionally flip the high byte of addresses where bit 23
 * is set.
 */
class inno_exe_decoder_5200 {
	
public:
	
	/*!
	 * \param flip_high_bytes true if the high byte of addresses is flipped if bit 23 is set.
	 *                        This optimization is used in Inno Setup 5.3.9 and later.
	 */
	explicit inno_exe_decoder_5200(bool flip_high_bytes)
		: flip_high_byte(flip_high_bytes), offset(0) { }
	
	/*!
	 * Decode data in place.
	 *
	 * \param final true if this is the end of the data.
	 *
	 * \return the number of decoded bytes. Unless final is true, up to 4 bytes at the end
	 *         (an instruction whose address continues in the next buffer) can be left
	 *         undecoded and must be passed again at the start of the next call.
	 */
	size_t decode(boost::uint8_t * data, size_t size, bool final);
	
private:
	
	static const size_t block_size = 0x10000;
	const bool flip_high_byte;
	
	boost::uint32_t offset; //! Total number of decoded bytes.
	
};

// Implementation:

inline void inno_exe_decoder_4108::decode(boost::uint8_t * data, size_t size) {
	
	for(size_t i = 0; i < size; i++, addr_offset++) {
		
		boost::uint8_t byte = data[i];
		
		if(addr_bytes_left == 0) {
			
			// Check if this is a CALL or JMP instruction.
			if(byte == 0xe8 || byte == 0xe9) {
				addr = ~addr_offset + 1;
				addr_bytes_left = 4;
			}
			
		} else {
			addr += byte;
			data[i] = boost::uint8_t(addr);
			addr >>= 8;
			addr_bytes_left--;
		}
		
	}
	
}

inline size_t inno_exe_decoder_5200::decode(boost::uint8_t * data, size_t size, bool final) {
	
	size_t i = 0;
	
	while(i < size) {
		
		// Check if this is a CALL or JMP instruction.
		const boost::uint8_t byte = data[i];
		if(byte != 0xe8 && byte != 0xe9) {
			i++, offset++;
			continue;
		}
		
		const size_t block_size_left = block_size - (offset % block_size);
		if(block_size_left < 5) {
			// Ignore instructions that span blocks.
			i++, offset++;
			continue;
		}
		
		// Need all four address bytes.
		if(size - i < 5) {
			if(!final) {
				return i;
			}
			// The data ends inside the address: leave it unchanged.
			offset += boost::uint32_t(size - i);
			return size;
		}
		
		boost::uint8_t * buffer = data + i + 1;
		offset += 5;
		i += 5;
		
		// Verify that the high byte of the address is 0x00 or 0xff.
		if(buffer[3] == 0x00 || buffer[3] == 0xff) {
			
			boost::uint32_t addr = offset & 0xffffff; // may wrap, but OK
			
			boost::uint32_t rel = buffer[0] | (boost::uint32_t(buffer[1]) << 8)
			                                | (boost::uint32_t(buffer[2]) << 16);
			rel -= addr;
			buffer[0] = boost::uint8_t(rel);
			buffer[1] = boost::uint8_t(rel >> 8);
			buffer[2] = boost::uint8_t(rel >> 16);
			
			if(flip_high_byte) {
				// For a slightly higher compression ratio, we want the resulting high
				// byte to be 0x00 for both forward and backward jumps. The high byte
				// of the original relative address is likely to be the sign extension
				// of bit 23, so if bit 23 is set, toggle all bits in the high byte.
				if(rel & 0x800000) {
					buffer[3] = boost::uint8_t(~buffer[3]);
				}
			}
			
		} else {
			// This is most likely not a CALL or JUMP.
		}
		
	}
	
	return size;
}

} // namespace stream

#endif // INNOEXTRACT_STREAM_EXEFILTER_HPP
