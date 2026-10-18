// Origin: innoextract src/setup/info.hpp, commit 6e9e34ed08 (master, 2025-02-06).
// Modified for the 7-Zip Inno plugin.
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
 * Central point to load all the different headers in the correct order.
 */
#ifndef INNOEXTRACT_SETUP_INFO_HPP
#define INNOEXTRACT_SETUP_INFO_HPP

#include <string>
#include <vector>
#include <iosfwd>

#include "../setup/header.hpp"
#include "../setup/version.hpp"
#include "../util/encoding.hpp"
#include "../util/flags.hpp"

namespace setup {

struct component_entry;
struct data_entry;
struct delete_entry;
struct directory_entry;
struct file_entry;
struct icon_entry;
struct ini_entry;
struct language_entry;
struct message_entry;
struct permission_entry;
struct registry_entry;
struct run_entry;
struct task_entry;
struct type_entry;

/*!
 * Class used to hold and load the various \ref setup headers.
 */
struct info {
	
	// Explicit constructor/destructor required to allow forward-declaring entry types
	info();
	~info();
	
	FLAGS(entry_types,
		Components,
		DataEntries,
		DeleteEntries,
		UninstallDeleteEntries,
		Directories,
		Files,
		Icons,
		IniEntries,
		Languages,
		Messages,
		Permissions,
		RegistryEntries,
		RunEntries,
		UninstallRunEntries,
		Tasks,
		Types,
		WizardImages,
		DecompressorDll,
		DecryptDll,
		NoSkip,
		NoUnknownVersion
	);
	
	setup::version version;
	
	//! What the encryption header of Inno Setup 6.5.0 and newer says is encrypted.
	enum encryption_use {
		EncryptionNone,  //!< Nothing is encrypted, or the version has no encryption header.
		EncryptionFiles, //!< The file data is encrypted.
		EncryptionFull,  //!< The file data and the setup headers are encrypted.
	};
	
	encryption_use encryption;
	
	util::codepage_id codepage;
	
	setup::header header;
	
	std::vector<component_entry>  components;               //! \c Components
	std::vector<data_entry>       data_entries;             //! \c DataEntries
	std::vector<delete_entry>     delete_entries;           //! \c DeleteEntries
	std::vector<delete_entry>     uninstall_delete_entries; //! \c UninstallDeleteEntries
	std::vector<directory_entry>  directories;              //! \c Directories
	std::vector<file_entry>       files;                    //! \c Files
	std::vector<icon_entry>       icons;                    //! \c Icons
	std::vector<ini_entry>        ini_entries;              //! \c IniEntries
	std::vector<language_entry>   languages;                //! \c Languages
	std::vector<message_entry>    messages;                 //! \c Messages
	std::vector<permission_entry> permissions;              //! \c Permissions
	std::vector<registry_entry>   registry_entries;         //! \c RegistryEntries
	std::vector<run_entry>        run_entries;              //! \c RunEntries
	std::vector<run_entry>        uninstall_run_entries;    //! \c UninstallRunEntries
	std::vector<task_entry>       tasks;                    //! \c Tasks
	std::vector<type_entry>       types;                    //! \c Types
	
	//! Images displayed in the installer UI (BMP files, or PNG files in newer versions).
	//! Loading enabled by \c WizardImages
	std::vector<std::string> wizard_images;
	std::vector<std::string> wizard_images_small;
	std::vector<std::string> wizard_images_back;       //!< introduced in 6.7.0
	std::vector<std::string> wizard_images_dark;       //!< introduced in 6.6.0
	std::vector<std::string> wizard_images_small_dark; //!< introduced in 6.6.0
	std::vector<std::string> wizard_images_back_dark;  //!< introduced in 6.7.0
	
	//! Contents of the helper DLL used to decompress setup data in some versions.
	//! Loading enabled by \c DecompressorDll
	std::string decompressor_dll;
	
	//! Contents of the helper DLL used to decrypt setup data.
	//! Loading enabled by \c DecryptDll
	std::string decrypt_dll;
	
	/*!
	 * Load setup headers.
	 *
	 * \param is      The input stream to load the setup headers from.
	 *                It must already be positioned at start of \ref setup::version
	 *                identifier whose position is given by
	 *                \ref loader::offsets::header_offset.
	 * \param entries What kinds of entries to load.
	 * \param force_codepage Windows codepage to use for strings in ANSI installers.
	 */
	void load(std::istream & is, entry_types entries, util::codepage_id force_codepage = 0);
	
	/*!
	 * Load the setup data version and, for Inno Setup 6.5.0 and newer, the encryption header.
	 *
	 * \param is The input stream, positioned at \ref loader::offsets::header_offset.
	 *
	 * Afterwards \ref encryption tells if \ref load_headers needs a key. Nothing else is
	 * read for unknown versions that are newer than all known ones
	 * (see \ref version::is_newer_than_known).
	 */
	void load_version(std::istream & is);
	
	/*!
	 * Load the setup headers following the data read by \ref load_version.
	 *
	 * \param key Key from \ref get_key if \ref encryption is \c EncryptionFull.
	 *
	 * The other parameters are the same as for \ref load.
	 */
	void load_headers(std::istream & is, entry_types entries, util::codepage_id force_codepage = 0,
	                  const std::string & key = std::string());
	
	/*!
	 * Derive the key used to decrypt the setup data from a password (UTF-8).
	 *
	 * \return the password in the encoding of the installer (ARC4, before Inno Setup 6.4.0)
	 *         or the XChaCha20 key followed by the base nonce (6.4.0 and newer).
	 */
	std::string get_key(const std::string & password) const;
	
	//! Check a key from \ref get_key against the password check stored in the setup headers.
	bool check_key(const std::string & key) const;
	
private:
	
	//! Encryption header data (6.5.0 and newer): KDF salt, KDF iterations and base nonce
	std::string encryption_salt;
	//! Encryption header data (6.5.0 and newer): the password test
	char encryption_password_test[4];
	
	void load_encryption_header(std::istream & is);
	
	//! Store the encryption header in \ref header after loading it.
	void apply_encryption_header();
	
	/*!
	 * Load setup headers for a specific version.
	 *
	 * \param is      The input stream to load the setup headers from.
	 *                It must already be positioned at start of the compressed headers.
	 *                The compressed headers start directly after the \ref setup::version
	 *                identifier whose position is given by
	 *                \ref loader::offsets::header_offset.
	 * \param entries What kinds of entries to load.
	 * \param force_codepage Windows codepage to use for strings in ANSI installers.
	 *
	 * This function does not set the \ref version member.
	 */
	void try_load(std::istream & is, entry_types entries, util::codepage_id force_codepage,
	              const std::string & key);
	
	template <class Entry>
	void load_entries(std::istream & is, entry_types entries, size_t count,
	                  std::vector<Entry> & result, entry_types::enum_type entry_type);
	
};

} // namespace setup

FLAGS_OVERLOADS(setup::info::entry_types)

#endif // INNOEXTRACT_SETUP_INFO_HPP
