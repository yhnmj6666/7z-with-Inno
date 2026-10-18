// Origin: innoextract src/setup/info.cpp, commit 6e9e34ed08 (master, 2025-02-06).
// Modified for the 7-Zip Inno plugin.
/*
 * Copyright (C) 2011-2020 Daniel Scharrer
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

#include "../setup/info.hpp"

#include <cassert>
#include <cstring>
#include <istream>
#include <sstream>

//#include <boost/foreach.hpp>

#include "../crypto/crc32.hpp"
#include "../crypto/hasher.hpp"
#include "../crypto/pbkdf2.hpp"
#include "../crypto/sha256.hpp"
#include "../crypto/xchacha20.hpp"
#include "../setup/component.hpp"
#include "../setup/data.hpp"
#include "../setup/delete.hpp"
#include "../setup/directory.hpp"
#include "../setup/file.hpp"
#include "../setup/icon.hpp"
#include "../setup/ini.hpp"
#include "../setup/item.hpp"
#include "../setup/language.hpp"
#include "../setup/message.hpp"
#include "../setup/permission.hpp"
#include "../setup/registry.hpp"
#include "../setup/run.hpp"
#include "../setup/task.hpp"
#include "../setup/type.hpp"
#include "../stream/block.hpp"
#include "../util/endian.hpp"
#include "../util/load.hpp"
#include "../util/log.hpp"
#include "../util/output.hpp"

namespace setup {

template <class Entry>
void info::load_entries(std::istream & is, entry_types entries, size_t count,
                        std::vector<Entry> & result, entry_types::enum_type entry_type) {
	
	result.clear();
	if(entries & entry_type) {
		result.resize(count);
		for(size_t i = 0; i < count; i++) {
			result[i].load(is, *this);
		}
	} else {
		for(size_t i = 0; i < count; i++) {
			Entry entry;
			entry.load(is, *this);
		}
	}
}

namespace {

void load_wizard_images(std::istream & is, const setup::version & version,
                        std::vector<std::string> & images, info::entry_types entries) {
	
	size_t count = 1;
	if(version >= INNO_VERSION(5, 6, 0)) {
		// 6.6.1+: -1 if the dark images are the same as the regular ones
		boost::int32_t stored_count = util::load<boost::int32_t>(is);
		count = stored_count < 0 ? 0 : size_t(stored_count);
	}
	
	if(entries & (info::WizardImages | info::NoSkip)) {
		images.clear();
		for(size_t i = 0; i < count; i++) {
			images.push_back(std::string());
			is >> util::binary_string(images.back());
		}
		if(version < INNO_VERSION(5, 6, 0) && images[0].empty()) {
			images.clear();
		}
	} else {
		for(size_t i = 0; i < count; i++) {
			util::binary_string::skip(is);
		}
	}
	
}

void load_wizard_and_decompressor(std::istream & is, const setup::version & version,
                                  const setup::header & header,
                                  setup::info & info, info::entry_types entries) {
	
	info.wizard_images.clear();
	info.wizard_images_small.clear();
	info.wizard_images_back.clear();
	info.wizard_images_dark.clear();
	info.wizard_images_small_dark.clear();
	info.wizard_images_back_dark.clear();
	
	load_wizard_images(is, version, info.wizard_images, entries);
	
	if(version >= INNO_VERSION(2, 0, 0) || version.is_isx()) {
		load_wizard_images(is, version, info.wizard_images_small, entries);
	}
	
	if(version >= INNO_VERSION(6, 7, 0)) {
		load_wizard_images(is, version, info.wizard_images_back, entries);
	}
	if(version >= INNO_VERSION(6, 6, 0)) {
		load_wizard_images(is, version, info.wizard_images_dark, entries);
		load_wizard_images(is, version, info.wizard_images_small_dark, entries);
	}
	if(version >= INNO_VERSION(6, 7, 0)) {
		load_wizard_images(is, version, info.wizard_images_back_dark, entries);
	}
	
	info.decompressor_dll.clear();
	if(header.compression == stream::BZip2
	   || (header.compression == stream::LZMA1 && version == INNO_VERSION(4, 1, 5))
	   || (header.compression == stream::Zlib && version >= INNO_VERSION(4, 2, 6))) {
		if(entries & (info::DecompressorDll | info::NoSkip)) {
			is >> util::binary_string(info.decompressor_dll);
		} else {
			// decompressor dll - we don't need this
			util::binary_string::skip(is);
		}
	}
	
	if(!header.seven_zip_library_name.empty()) {
		// 7-Zip library used to extract archives (6.5.0+) - we don't need this
		util::binary_string::skip(is);
	}
	
	info.decrypt_dll.clear();
	if((header.options & header::EncryptionUsed) && version < INNO_VERSION(6, 4, 0)) {
		if(entries & (info::DecryptDll | info::NoSkip)) {
			is >> util::binary_string(info.decrypt_dll);
		} else {
			// decrypt dll - we don't need this
			util::binary_string::skip(is);
		}
	}
	
}

void check_is_end(stream::block_reader::pointer & is, const char * what) {
	is->exceptions(std::ios_base::goodbit);
	char dummy;
	if(!is->get(dummy).eof()) {
		throw std::ios_base::failure(what);
	}
}

} // anonymous namespace

void info::try_load(std::istream & is, entry_types entries, util::codepage_id force_codepage,
                    const std::string & key) {
	
	debug("trying to load setup headers for version " << version);
	
	if((entries & (Messages | NoSkip)) || (!version.is_unicode() && !force_codepage)) {
		entries |= Languages;
	}
	
	// Inno Setup XORs -2 and -3 into the nonce of the two encrypted header blocks
	const std::string & block_key = (encryption == EncryptionFull) ? key : std::string();
	
	std::shared_ptr<size_t> header_size = std::make_shared<size_t>();
	stream::block_reader::pointer reader = stream::block_reader::get(is, version, header_size.get(),
	                                                                 block_key, boost::uint32_t(-2));
	header.header_size = *header_size;
	
	debug("loading main header");
	header.load(*reader, version);
	if(version >= INNO_VERSION(6, 5, 0)) {
		apply_encryption_header();
	}
	
	debug("loading languages");
	load_entries(*reader, entries, header.language_count, languages, Languages);
	
	debug("determining encoding");
	if(version.is_unicode()) {
		// Unicode installers are always UTF16-LE, do not allow users to override that.
		codepage = util::cp_utf16le;
	} else if(force_codepage) {
		codepage = force_codepage;
	} else if(languages.empty()) {
		codepage = util::cp_windows1252;
	} else {
		// Non-Unicode installers do not have a defined codepage but instead just assume the
		// codepage of the system the installer is run on.
		// Look at the list of available languages to guess a suitable codepage.
		codepage = languages[0].codepage;
		BOOST_FOREACH(const language_entry & language, languages) {
			if(language.codepage == util::cp_windows1252) {
				codepage = util::cp_windows1252;
				break;
			}
		}
	}
	
	header.decode(codepage);
	BOOST_FOREACH(language_entry & language, languages) {
		language.decode(codepage);
	}
	
	if(version < INNO_VERSION(4, 0, 0)) {
		debug("loading images and plugins");
		load_wizard_and_decompressor(*reader, version, header, *this, entries);
	}
	
	debug("loading messages");
	load_entries(*reader, entries, header.message_count, messages, Messages);
	debug("loading permissions");
	load_entries(*reader, entries, header.permission_count, permissions, Permissions);
	debug("loading types");
	load_entries(*reader, entries, header.type_count, types, Types);
	debug("loading components");
	load_entries(*reader, entries, header.component_count, components, Components);
	debug("loading tasks");
	load_entries(*reader, entries, header.task_count, tasks, Tasks);
	debug("loading directories");
	load_entries(*reader, entries, header.directory_count, directories, Directories);
	if(version >= INNO_VERSION(6, 5, 0)) {
		debug("loading issig keys");
		for(size_t i = 0; i < header.issig_key_count; i++) {
			// PublicX, PublicY and RuntimeID - we don't need these
			util::binary_string::skip(*reader);
			util::binary_string::skip(*reader);
			util::binary_string::skip(*reader);
		}
	}
	debug("loading files");
	load_entries(*reader, entries, header.file_count, files, Files);
	debug("loading icons");
	load_entries(*reader, entries, header.icon_count, icons, Icons);
	debug("loading ini entries");
	load_entries(*reader, entries, header.ini_entry_count, ini_entries, IniEntries);
	debug("loading registry entries");
	load_entries(*reader, entries, header.registry_entry_count, registry_entries, RegistryEntries);
	debug("loading delete entries");
	load_entries(*reader, entries, header.delete_entry_count, delete_entries, DeleteEntries);
	debug("loading uninstall delete entries");
	load_entries(*reader, entries, header.uninstall_delete_entry_count, uninstall_delete_entries,
	             UninstallDeleteEntries);
	debug("loading run entries");
	load_entries(*reader, entries, header.run_entry_count, run_entries, RunEntries);
	debug("loading uninstall run entries");
	load_entries(*reader, entries, header.uninstall_run_entry_count, uninstall_run_entries,
	             UninstallRunEntries);
	
	if(version >= INNO_VERSION(4, 0, 0)) {
		debug("loading images and plugins");
		load_wizard_and_decompressor(*reader, version, header, *this, entries);
	}
	
	// restart the compression stream
	check_is_end(reader, "unknown data at end of primary header stream");
	reader = stream::block_reader::get(is, version, header_size.get(), block_key, boost::uint32_t(-3));
	header.header_size += *header_size;

	debug("loading data entries");
	load_entries(*reader, entries, header.data_entry_count, data_entries, DataEntries);
	
	check_is_end(reader, "unknown data at end of secondary header stream");
}

void info::load(std::istream & is, entry_types entries, util::codepage_id force_codepage) {
	
	load_version(is);
	
	load_headers(is, entries, force_codepage);
	
}

void info::load_version(std::istream & is) {
	
	version.load(is);
	
	encryption = EncryptionNone;
	encryption_salt.clear();
	std::memset(encryption_password_test, 0, sizeof(encryption_password_test));
	
	if(version >= INNO_VERSION(6, 5, 0) && !version.is_newer_than_known()) {
		load_encryption_header(is);
	}
	
}

void info::load_encryption_header(std::istream & is) {
	
	// CRC32 of the encryption header, then TSetupEncryptionHeader: EncryptionUse (1 byte),
	// KDFSalt (16 bytes), KDFIterations (4 bytes), BaseNonce (24 bytes), PasswordTest (4 bytes)
	char data[4 + 1 + 44 + 4];
	is.read(data, std::streamsize(sizeof(data)));
	if(is.fail()) {
		throw std::runtime_error("could not read the encryption header");
	}
	
	crypto::crc32 checksum;
	checksum.init();
	checksum.update(data + 4, sizeof(data) - 4);
	if(checksum.finalize() != util::little_endian::load<boost::uint32_t>(data)) {
		throw std::runtime_error("encryption header CRC32 mismatch");
	}
	
	switch(boost::uint8_t(data[4])) {
		case 0: encryption = EncryptionNone; break;
		case 1: encryption = EncryptionFiles; break;
		case 2: encryption = EncryptionFull; break;
		default: throw std::runtime_error("unknown encryption type");
	}
	
	encryption_salt.assign(data + 5, 44);
	std::memcpy(encryption_password_test, data + 5 + 44, sizeof(encryption_password_test));
	
	apply_encryption_header();
	
}

void info::apply_encryption_header() {
	
	header.password.type = crypto::PBKDF2_SHA256_XChaCha20;
	std::memcpy(header.password.check, encryption_password_test, sizeof(header.password.check));
	header.password_salt = encryption_salt;
	
	if(encryption != EncryptionNone) {
		header.options |= header::EncryptionUsed;
	}
	
}

void info::load_headers(std::istream & is, entry_types entries, util::codepage_id force_codepage,
                        const std::string & key) {
	
	if(!version.known) {
		if(entries & NoUnknownVersion) {
			std::ostringstream oss;
			oss << "Unexpected setup data version: " << version;
			throw std::runtime_error(oss.str());
		}
		log_warning << "Unexpected setup data version: "
		            << color::white << version << color::reset;
	}
	
	version_constant listed_version = version.value;
	
	// Some setup versions didn't increment the data version number when they should have.
	// To work around this, we try to parse the headers for all data versions and use the first
	// version that parses without warnings or errors.
	bool ambiguous = !version.known || version.is_ambiguous();
	if(version.is_ambiguous()) {
		// Force parsing all headers so that we don't miss any errors.
		entries |= NoSkip;
	}
	
	bool parsed_without_errors = false;
	std::streampos start = is.tellg();
	for(;;) {
		
		warning_suppressor warnings;
		
		try {
			
			// Try to parse headers for this version
			try_load(is, entries, force_codepage, key);
			
			if(warnings) {
				// Parsed without errors but with warnings - try other versions first
				if(!parsed_without_errors) {
					listed_version = version.value;
					parsed_without_errors = true;
				}
				throw std::exception();
			}
			
			warnings.flush();
			return;
			
		} catch(...) {
			
			is.clear();
			is.seekg(start);
			
			version_constant next_version = version.next();
			
			if(!ambiguous || !next_version) {
				if(version.value != listed_version) {
					// Rewind to a previous version that had better results and report those
					version.value = listed_version;
					warnings.restore();
					try_load(is, entries, force_codepage, key);
				} else {
					// Otherwise. report results for the current version
					warnings.flush();
					if(!parsed_without_errors) {
						throw;
					}
				}
				return;
			}
			
			// Retry with the next version
			version.value = next_version;
			ambiguous = version.is_ambiguous();
			
		}
		
	}
	
}

std::string info::get_key(const std::string & password) const {
	
	std::string encoded_password;
	util::codepage_id cp = codepage;
	if(version.is_unicode()) {
		cp = util::cp_utf16le;
	}
	util::from_utf8(password, encoded_password, cp);
	
	if(header.password.type == crypto::PBKDF2_SHA256_XChaCha20) {
		
		// 16 bytes PBKDF2 salt + 4 bytes PBKDF2 iterations + 24 bytes XChaCha20 base nonce
		if(header.password_salt.length() != 20 + crypto::xchacha20::nonce_size) {
			throw std::runtime_error("unexpected password salt size");
		}
		
		std::string result;
		result.resize(crypto::xchacha20::key_size + crypto::xchacha20::nonce_size);
		typedef crypto::pbkdf2<crypto::sha256> pbkdf2;
		pbkdf2::derive(encoded_password.c_str(), encoded_password.length(), &header.password_salt[0], 16,
		               util::little_endian::load<boost::uint32_t>(&header.password_salt[16]), &result[0],
		               crypto::xchacha20::key_size);
		
		std::memcpy(&result[crypto::xchacha20::key_size], &header.password_salt[20],
		            crypto::xchacha20::nonce_size);
		
		return result;
	}
	
	return encoded_password;
}

bool info::check_key(const std::string & key) const {
	
	if(header.password.type == crypto::PBKDF2_SHA256_XChaCha20) {
		
		if(key.length() != crypto::xchacha20::key_size + crypto::xchacha20::nonce_size) {
			throw std::runtime_error("unexpected key size");
		}
		
		// The password test is a zero 32-bit value encrypted with first slice -1
		crypto::xchacha20 cipher;
		cipher.init(key, 0, boost::uint32_t(-1));
		
		char buffer[] = { 0, 0, 0, 0 };
		cipher.crypt(buffer, buffer, sizeof(buffer));
		
		return (std::memcmp(buffer, header.password.check, sizeof(buffer)) == 0);
		
	} else {
		
		crypto::hasher checksum(header.password.type);
		checksum.update(header.password_salt.c_str(), header.password_salt.length());
		checksum.update(key.c_str(), key.length());
		return (checksum.finalize() == header.password);
		
	}
	
}

info::info() : encryption(EncryptionNone), codepage(0) {
	std::memset(encryption_password_test, 0, sizeof(encryption_password_test));
}
info::~info() { }

} // namespace setup
