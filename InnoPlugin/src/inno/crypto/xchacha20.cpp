// Origin: innoextract src/crypto/xchacha20.cpp, commit 6e9e34ed08 (master, 2025-02-06).
// Modified for the 7-Zip Inno plugin.
#include "../minihead.hpp"
/*
 * Copyright (C) 2024 Daniel Scharrer
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

// Based on Inno Setup's ChaCha20.pas which is in turn based on https://github.com/Ginurx/chacha20-c

#include "../crypto/xchacha20.hpp"

#include <algorithm>
#include <cstring>

#include "../util/endian.hpp"
#include "../util/math.hpp"

namespace crypto {

void xchacha20::init(const char key[key_size], const char nonce[nonce_size]) {
	
	char subkey[key_size];
	derive_subkey(key, nonce, subkey);
	init_state(state, subkey);
	state[12] = 0;
	state[13] = 0;
	state[14] = util::little_endian::load<word>(nonce + 16);
	state[15] = util::little_endian::load<word>(nonce + 20);
	pos = sizeof(keystream);
	
}

void xchacha20::init(const std::string & key, boost::uint64_t start_offset,
                     boost::uint32_t first_slice) {
	
	// The caller ensures key.size() == key_size + nonce_size
	char nonce[nonce_size];
	std::memcpy(nonce, key.data() + key_size, nonce_size);
	util::little_endian::store(util::little_endian::load<boost::uint64_t>(nonce) ^ start_offset, nonce);
	util::little_endian::store(util::little_endian::load<boost::uint32_t>(nonce + 8) ^ first_slice,
	                           nonce + 8);
	init(key.data(), nonce);
	
}

void xchacha20::discard(size_t length) {
	
	// asume pos > 0 && pos <= sizeof(keystream)
	
	if(pos != sizeof(keystream)) {
		size_t remaining = std::min(length, size_t(64) - size_t(pos));
		pos = boost::uint8_t(pos + remaining);
		length -= remaining;
	}
	
	// assert length == 0 || pos == sizeof(keystream)
	
	increment_count(state, length / sizeof(keystream));
	
	if(length % sizeof(keystream)) {
		update();
		pos = boost::uint8_t(length % sizeof(keystream));
	}
	
}

void xchacha20::crypt(const char * in, char * out, size_t length) {
	
	// asume pos > 0 && pos <= 64
	
	for(size_t i = 0; i < length; i++, pos++) {
		if(pos == sizeof(keystream)) {
			update();
			pos = 0;
		}
		boost::uint8_t key = boost::uint8_t(keystream[pos / sizeof(word)] >> ((pos % sizeof(word)) * 8));
		out[i] = char(boost::uint8_t(in[i]) ^ key);
	}
	
}


void xchacha20::update() {
	
	std::memcpy(keystream, state, sizeof(state));
	run_rounds(keystream);
	
	for(size_t i = 0; i < 16; i++) {
		keystream[i] += state[i];
	}
	
	increment_count(state);
	
}

void xchacha20::derive_subkey(const char key[key_size], const char nonce[16], char subkey[key_size]) {
	
	word state[16];
	init_state(state, key);
	state[12] = util::little_endian::load<word>(nonce);
	state[13] = util::little_endian::load<word>(nonce + 4);
	state[14] = util::little_endian::load<word>(nonce + 8);
	state[15] = util::little_endian::load<word>(nonce + 12);
	run_rounds(state);
	util::little_endian::store<word>(state[0], subkey);
	util::little_endian::store<word>(state[1], subkey + 4);
	util::little_endian::store<word>(state[2], subkey + 8);
	util::little_endian::store<word>(state[3], subkey + 12);
	util::little_endian::store<word>(state[12], subkey + 16);
	util::little_endian::store<word>(state[13], subkey + 20);
	util::little_endian::store<word>(state[14], subkey + 24);
	util::little_endian::store<word>(state[15], subkey + 28);
	
}


void xchacha20::init_state(word state[16], const char key[key_size]) {
	
	state[0] = 0x61707865;
	state[1] = 0x3320646e;
	state[2] = 0x79622d32;
	state[3] = 0x6b206574;
	for(size_t i = 0; i < 8; i++) {
		state[4 + i] = util::little_endian::load<word>(key + i * sizeof(word));
	}
	
}

void xchacha20::run_rounds(word keystream[16]) {
	
	#define CHACHA20_QR(a, b, c, d) \
		a += b; \
		d = d ^ a; \
		d = util::rotl_fixed(d, 16); \
		c += d; \
		b = b ^ c; \
		b = util::rotl_fixed(b, 12); \
		a += b; \
		d = d ^ a; \
		d = util::rotl_fixed(d, 8); \
		c += d; \
		b = b ^ c; \
		b = util::rotl_fixed(b, 7);
	
	for(size_t i = 0; i < 10; i++) {
		CHACHA20_QR(keystream[0], keystream[4], keystream[8], keystream[12]);  // column 0
		CHACHA20_QR(keystream[1], keystream[5], keystream[9], keystream[13]);  // column 1
		CHACHA20_QR(keystream[2], keystream[6], keystream[10], keystream[14]); // column 2
		CHACHA20_QR(keystream[3], keystream[7], keystream[11], keystream[15]); // column 3
		CHACHA20_QR(keystream[0], keystream[5], keystream[10], keystream[15]); // diagonal 1 (main diagonal)
		CHACHA20_QR(keystream[1], keystream[6], keystream[11], keystream[12]); // diagonal 2
		CHACHA20_QR(keystream[2], keystream[7], keystream[8], keystream[13]);  // diagonal 3
		CHACHA20_QR(keystream[3], keystream[4], keystream[9], keystream[14]);  // diagonal 4
	}
	
	#undef CHACHA20_QR
	
}

void xchacha20::increment_count(word state[16], size_t increment) {
	
	boost::uint64_t count = (boost::uint64_t(state[13]) << (sizeof(word) * 8)) + state[12];
	count += increment;
	state[12] = word(count);
	state[13] = word(count >> (sizeof(word) * 8));
	
}

} // namespace crypto
