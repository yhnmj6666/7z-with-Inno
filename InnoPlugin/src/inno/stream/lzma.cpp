// Origin: innoextract src/stream/lzma.cpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: decodes with C/LzmaDec.c and C/Lzma2Dec.c
// from 7-Zip instead of liblzma.
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

#include "../stream/lzma.hpp"

#include "C/Alloc.h"
#include "C/LzmaDec.h"
#include "C/Lzma2Dec.h"

namespace stream {

bool lzma_decompressor_impl_base::filter(const char * & begin_in, const char * end_in,
                                         char * & begin_out, char * end_out, bool flush) {
	
	SizeT in_size = SizeT(end_in - begin_in);
	SizeT out_size = SizeT(end_out - begin_out);
	const Byte * in = reinterpret_cast<const Byte *>(begin_in);
	Byte * out = reinterpret_cast<Byte *>(begin_out);
	
	ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
	SRes res;
	if(lzma2) {
		res = Lzma2Dec_DecodeToBuf(static_cast<CLzma2Dec *>(stream), out, &out_size,
		                           in, &in_size, LZMA_FINISH_ANY, &status);
	} else {
		res = LzmaDec_DecodeToBuf(static_cast<CLzmaDec *>(stream), out, &out_size,
		                          in, &in_size, LZMA_FINISH_ANY, &status);
	}
	
	// No progress although there is room for output: the input ended too early
	// (liblzma reported this as LZMA_BUF_ERROR).
	if(flush && res == SZ_OK && status != LZMA_STATUS_FINISHED_WITH_MARK
	   && in_size == 0 && out_size == 0 && begin_out != end_out) {
		throw lzma_error("truncated lzma stream", SZ_ERROR_INPUT_EOF);
	}
	
	begin_in += in_size;
	begin_out += out_size;
	
	if(res != SZ_OK) {
		throw lzma_error("lzma decrompression error", res);
	}
	
	return (status != LZMA_STATUS_FINISHED_WITH_MARK);
}

void lzma_decompressor_impl_base::close() {
	
	if(stream) {
		if(lzma2) {
			CLzma2Dec * dec = static_cast<CLzma2Dec *>(stream);
			Lzma2Dec_Free(dec, &g_Alloc);
			delete dec;
		} else {
			CLzmaDec * dec = static_cast<CLzmaDec *>(stream);
			LzmaDec_Free(dec, &g_Alloc);
			delete dec;
		}
		stream = NULL;
	}
}

bool inno_lzma1_decompressor_impl::filter(const char * & begin_in, const char * end_in,
                                          char * & begin_out, char * end_out, bool flush) {
	
	// Decode the header.
	if(!stream) {
		
		// Read enough bytes to decode the header.
		while(nread != 5) {
			if(begin_in == end_in) {
				return true;
			}
			header[nread++] = *begin_in++;
		}
		
		// Properties byte (lc, lp, pb) and dictionary size (little endian), as in
		// the LZMA Alone format but without the uncompressed size field.
		boost::uint8_t properties = boost::uint8_t(header[0]);
		if(properties > (9 * 5 * 5)) {
			throw lzma_error("inno lzma1 property error", SZ_ERROR_UNSUPPORTED);
		}
		
		CLzmaDec * dec = new CLzmaDec;
		LzmaDec_CONSTRUCT(dec)
		SRes res = LzmaDec_Allocate(dec, reinterpret_cast<const Byte *>(header), LZMA_PROPS_SIZE, &g_Alloc);
		if(res != SZ_OK) {
			delete dec;
			throw lzma_error("inno lzma init error", res);
		}
		LzmaDec_Init(dec);
		
		stream = dec;
		lzma2 = false;
	}
	
	return lzma_decompressor_impl_base::filter(begin_in, end_in, begin_out, end_out, flush);
}

bool inno_lzma2_decompressor_impl::filter(const char * & begin_in, const char * end_in,
                                          char * & begin_out, char * end_out, bool flush) {
	
	// Decode the header.
	if(!stream) {
		
		if(begin_in == end_in) {
			return true;
		}
		
		// Dictionary size property byte.
		boost::uint8_t prop = boost::uint8_t(*begin_in++);
		if(prop > 40) {
			throw lzma_error("inno lzma2 property error", SZ_ERROR_UNSUPPORTED);
		}
		
		CLzma2Dec * dec = new CLzma2Dec;
		Lzma2Dec_CONSTRUCT(dec)
		SRes res = Lzma2Dec_Allocate(dec, Byte(prop), &g_Alloc);
		if(res != SZ_OK) {
			delete dec;
			throw lzma_error("inno lzma init error", res);
		}
		Lzma2Dec_Init(dec);
		
		stream = dec;
		lzma2 = true;
	}
	
	return lzma_decompressor_impl_base::filter(begin_in, end_in, begin_out, end_out, flush);
}

} // namespace stream
