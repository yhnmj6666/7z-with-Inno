// InnoScript.cpp
// Generates the files listed under "embedded\" that are not stored as is in
// the setup headers: the install script and the language files.

#include "StdAfx.h"

#include "InnoScript.h"

#include "inno/setup/component.hpp"
#include "inno/setup/delete.hpp"
#include "inno/setup/directory.hpp"
#include "inno/setup/file.hpp"
#include "inno/setup/icon.hpp"
#include "inno/setup/ini.hpp"
#include "inno/setup/language.hpp"
#include "inno/setup/message.hpp"
#include "inno/setup/registry.hpp"
#include "inno/setup/run.hpp"
#include "inno/setup/task.hpp"
#include "inno/setup/type.hpp"

#include <cctype>
#include <sstream>

namespace NArchive {
namespace NInno {

// Turns the stringified member expression "object.member_name" (or
// "member-name") into "MemberName".
static std::string OSS_Clean(const std::string &s)
{
	const size_t lastDot = s.rfind('.');
	const size_t start = (lastDot == std::string::npos) ? 0 : lastDot + 1;
	std::string name;
	bool wordStart = true;
	for (size_t i = start; i < s.size(); i++)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c == '_' || c == '-')
		{
			wordStart = true;
			continue;
		}
		name += wordStart ? (char)std::toupper(c) : (char)c;
		wordStart = false;
	}
	return name;
}

static std::string Flag_Clean(std::string s)
{
	auto wit = s.begin();
	bool write = false;
	for (auto rit = s.begin(); rit != s.end(); rit++)
	{
		if (isalnum(*rit))
		{
			if (write) 
			{
				*wit = *rit;
			}
			wit++;
		}
		else
		{
			write = true;
		}
	}
	s.resize(wit - s.begin());
	return s;
}

static std::string ReplaceString(std::string subject, const std::string& search, const std::string& replace) {
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
	return subject;
}

void GenerateInstallScript(const setup::info &info, std::string &out)
{
	using std::ostringstream;
	using std::endl;
	ostringstream oss;
	oss << "; InnoSetupVersion=" << info.version << endl;
	oss << endl;

	{
		oss << "[Setup]" << endl;
#		define _OSSL(s) if (!s.empty()) oss << OSS_Clean(#s) << "=" << s << "\n"
#		define _OSSL2(s,n) if (!s.empty()) oss << n << "=" << s << "\n"
		_OSSL(info.header.app_name);
		_OSSL(info.header.app_versioned_name);
		_OSSL(info.header.app_id);
		_OSSL(info.header.app_version);
		_OSSL(info.header.app_publisher);
		_OSSL(info.header.app_publisher_url);
		_OSSL(info.header.app_support_phone);
		_OSSL(info.header.app_support_url);
		_OSSL(info.header.app_updates_url);
		_OSSL(info.header.app_mutex);
		_OSSL(info.header.app_comments);
		_OSSL(info.header.app_modify_path);
		if (info.header.options & setup::header::CreateAppDir)
		{
			_OSSL(info.header.default_dir_name);
		}
		else
		{
			oss << "CreateAppDir=no\n";
		}
		if (info.header.default_group_name != "(Default)")
		{
			_OSSL(info.header.default_group_name);
		}
		_OSSL2(info.header.uninstall_icon, "UninstallDisplayIcon");
		_OSSL2(info.header.uninstall_icon_name,"UninstallDisplayName");
		
#		undef _OSSL
#		undef _OSSL2
		oss << "; uncompleted" << endl;
	}

	// Types
	if(!info.types.empty())
	{
		oss << "[Types]" << endl;  // optional
		for (const auto& i : info.types)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"';
			_OSS(i.description);
			_OSS(i.check);
#			undef _OSS
			if (i.custom_type)
			{
				oss << "; Flags: iscustom";
			}
			oss << endl;
		}
		oss << endl;
	}
	// Components
	if(!info.components.empty())
	{
		oss << "[Components]" << endl; // optional
		for (const auto& i : info.components)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s) << ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			_OSS(i.description);
			_OSS(i.types);
			_OSSN(i.extra_disk_pace_required);
			_OSS(i.check);
#			undef _OSS
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Tasks
	if (!info.tasks.empty())
	{
		oss << "[Tasks]" << endl; // optional
		for (const auto& i : info.tasks)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": " << s 
			_OSS(i.description);
			_OSS(i.group_description);
			_OSS2(i.components);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Dir
	if(!info.directories.empty())
	{
		oss << "[Dir]" << endl; // optional
		for (const auto& i : info.directories)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.permissions);
			//_OSS(i.attributes);
			_OSS(i.check);
#			undef _OSS
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// File
	{
		oss << "[File]" << endl;
		for (const auto& i : info.files)
		{
			oss << "Source: \"" << (i.source.empty() ? i.destination : i.source) << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			size_t sep=i.destination.find_last_of('\\');
			if (sep != std::string::npos)
			{
				std::string_view destDir = std::string_view(i.destination.data(),sep);
				std::string_view destName = std::string_view(i.destination.data()+sep+1);
				_OSS2(destDir, "DestDir");
				_OSS2(destName, "DestName");
			}
			else
			{
				_OSS2(i.destination,"DestDir");
			}
			_OSSN(i.external_size);
			//_OSS(i.attributes);
			//_OSS(i.permission);
			_OSS2(i.install_font_name,"FontInstall");
			_OSS(i.strong_assembly_name);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Icons
	{
		oss << "[Icons]" << endl;
		for (const auto& i : info.icons)
		{
			oss << "Name: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
#			define _OSSN(s) if (0!=s) oss << "; "<<OSS_Clean(#s) << ": " << s
			_OSS(i.filename);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			//_OSS(i.hotkey);
			_OSS(i.comment);
			_OSS2(i.icon_file,"IconFilename");
			_OSSN(i.icon_index);
			_OSS(i.app_user_model_id);
			_OSS(i.app_user_model_toast_activator_clsid);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
#			undef _OSSN
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// INI
	if(!info.ini_entries.empty())
	{
		oss << "[INI]" << endl; // optional
		for (const auto& i : info.ini_entries)
		{
			oss << "Filename: \"" << i.inifile << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.section);
			_OSS(i.key);
			_OSS2(i.value,"String");
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// InstallDelete
	if(!info.delete_entries.empty())
	{
		oss << "[InstallDelete]" << endl; // optional
		for (const auto& i : info.delete_entries)
		{
			using enum_type = std::remove_reference_t<decltype(i)>::target_type;
			static_assert(enum_names<enum_type>::named == 1);
			oss << "Type: " << Flag_Clean(enum_names<enum_type>::names[i.type]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.name);
			_OSS(i.check);
#			undef _OSS
			oss << endl;
		}
		oss << endl;
	}
	// Languages
	{
		oss << "[Languages]" << endl;
		for (const auto& i : info.languages)
		{
			oss << "Name: \"" << i.name << '"';
			oss << "; MessagesFile: \"embedded\\" << i.name << ".isl\"";
			if (!i.license_text.empty())
			{
				oss << "; LicenseFile: \"embedded\\" << i.name << ".rtf\"";
			}
			oss << endl;
		}
		oss << endl;
	}
	// CustomMessages
	if (!info.messages.empty())
	{
		oss << "[CustomMessages]" << endl; // optional
		for (const auto& i : info.messages)
		{
			using namespace std::literals;
			if (i.language != -1)
			{
				oss << info.languages[i.language].name << '.';
			}
			std::string unescaped_value = ReplaceString(i.value, "\n"s, "\\n"s);
			unescaped_value = ReplaceString(unescaped_value, "\r"s, "\\r"s);
			oss << i.name << '=' << unescaped_value << endl;
		}
		oss << endl;
	}
	// Registry
	if(!info.registry_entries.empty())
	{
		oss << "[Registry]" << endl;
		for (const auto& i : info.registry_entries)
		{
			using hive_name = std::remove_reference_t<decltype(i)>::hive_name;
			using value_type = std::remove_reference_t<decltype(i)>::value_type;
			static_assert(enum_names<hive_name>::named == 1);
			static_assert(enum_names<value_type>::named == 1);
			oss << "Root: " << Flag_Clean(enum_names<hive_name>::names[i.hive]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS2(i.key, "Subkey");
			oss << "; ValueType: " << Flag_Clean(enum_names<value_type>::names[i.type]);
			_OSS2(i.name, "ValueName");
			_OSS2(i.value,"ValueData");
			_OSS(i.permissions);
			_OSS(i.check);
#			undef _OSS			
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// Run
	if (!info.run_entries.empty())
	{
		oss << "[Run]" << endl; // optional
		for (const auto& i : info.run_entries)
		{
			oss << "Filename: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.description);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			_OSS2(i.status_message, "StatusMsg");
			_OSS(i.run_once_id);
			_OSS(i.verb);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}
	// UninstallDelete
	if (!info.uninstall_delete_entries.empty())
	{
		oss << "[UninstallDelete]" << endl; // optional
		for (const auto& i : info.uninstall_delete_entries)
		{
			using enum_type = std::remove_reference_t<decltype(i)>::target_type;
			static_assert(enum_names<enum_type>::named == 1);
			oss << "Type: " << Flag_Clean(enum_names<enum_type>::names[i.type]);
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
			_OSS(i.name);
			_OSS(i.check);
#			undef _OSS
			oss << endl;
		}
		oss << endl;
	}
	// Uninstall Run
	if (!info.uninstall_run_entries.empty())
	{
		oss << "[UninstallRun]" << endl; // optional
		for (const auto& i : info.uninstall_run_entries)
		{
			oss << "Filename: \"" << i.name << '"';
#			define _OSS(s) if (!s.empty()) oss << "; "<<OSS_Clean(#s)<<": \"" << s << '"'
#			define _OSS2(s,d) if (!s.empty()) oss << "; " d ": \"" << s << '"'
			_OSS(i.description);
			_OSS(i.parameters);
			_OSS(i.working_dir);
			_OSS2(i.status_message, "StatusMsg");
			_OSS(i.run_once_id);
			_OSS(i.verb);
			_OSS(i.check);
#			undef _OSS
#			undef _OSS2
			using enum_type = std::remove_reference_t<decltype(i)>::flags_Enum_;
			bool first_flag = true;
			for (size_t j = 0; j < i.options.bits; j++)
			{
				if (i.options.has(static_cast<enum_type>(j)))
				{
					if (first_flag)
					{
						oss << "; Flags:";
						first_flag = false;
					}
					static_assert(enum_names<enum_type>::named == 1);
					oss << ' ' << Flag_Clean(enum_names<enum_type>::names[j]);
				}
			}
			oss << endl;
		}
		oss << endl;
	}

	out = oss.str();
}

void GenerateLanguageFile(const setup::language_entry &lang, std::string &out)
{
	using std::ostringstream;
	using std::endl;
	ostringstream oss;
	oss << "[LangOptions]" << endl;
#	define _OSSL(s) if (!s.empty()) oss << OSS_Clean(#s) << "=" << s << "\n"
#	define _OSSLN(s) oss << OSS_Clean(#s) << "=" << s << "\n"
#	define _OSSL2(s,n) if (!s.empty()) oss << n << "=" << s << "\n"
#	define _OSSLN2(s,n) oss << n << "=" << s << "\n"
	_OSSL(lang.language_name);
	// use hex instead "${hex of language id}"?
	_OSSLN(lang.language_id);
	_OSSLN2(lang.codepage,"LanguageCodePage");
	_OSSL2(lang.dialog_font,"DialogFontName");
	_OSSL2(lang.title_font,"TitleFontName");
	_OSSL2(lang.welcome_font,"WelcomeFontName");
	_OSSL2(lang.copyright_font, "CopyrightFontName");
	_OSSLN(lang.dialog_font_size);
	_OSSLN(lang.title_font_size);
	_OSSLN(lang.welcome_font_size);
	_OSSLN(lang.copyright_font_size);
	_OSSLN(lang.right_to_left);
	_OSSL(lang.data);
#	undef _OSSL
#	undef _OSSLN
#	undef _OSSL2
#	undef _OSSLN2
	out = oss.str();
}

}}
