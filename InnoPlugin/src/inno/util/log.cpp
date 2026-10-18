// Origin: innoextract src/util/log.cpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: messages never go to the console; they
// are collected per thread (log_capture) or written to an opt-in debug log.
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

#include "../util/log.hpp"

#include <stdio.h>
#include <stdlib.h>

namespace {

//! Innermost log_capture of the current thread.
thread_local log_capture * current_capture = NULL;

/*!
 * The debug log file, from the INNO_PLUGIN_LOG environment variable.
 * Read once per process; empty if the variable is not set.
 */
#ifdef _WIN32
const std::wstring & debug_log_path() {
	static const std::wstring path = [] {
		const wchar_t * value = _wgetenv(L"INNO_PLUGIN_LOG");
		return std::wstring(value ? value : L"");
	}();
	return path;
}
#else
const std::string & debug_log_path() {
	static const std::string path = [] {
		const char * value = getenv("INNO_PLUGIN_LOG");
		return std::string(value ? value : "");
	}();
	return path;
}
#endif

void write_debug_log(logger::log_level level, const std::string & message) {
	
	if(debug_log_path().empty()) {
		return;
	}
	
	#ifdef _WIN32
	FILE * file = _wfopen(debug_log_path().c_str(), L"ab");
	#else
	FILE * file = fopen(debug_log_path().c_str(), "ab");
	#endif
	if(!file) {
		return;
	}
	
	const char * prefix = "";
	switch(level) {
		case logger::Debug:   prefix = "debug: "; break;
		case logger::Info:    prefix = "info: "; break;
		case logger::Warning: prefix = "warning: "; break;
		case logger::Error:   prefix = "error: "; break;
	}
	
	std::string line = prefix + message + "\n";
	fwrite(line.data(), 1, line.size(), file);
	fclose(file);
}

} // anonymous namespace

bool logger::debug_enabled() {
	return !debug_log_path().empty();
}

void logger::emit(log_level level, const std::string & message) {
	if(current_capture) {
		log_capture::message entry = { level, message };
		current_capture->messages.push_back(entry);
	} else {
		write_debug_log(level, message);
	}
}

logger::~logger() {
	try {
		emit(level, buffer.str());
	} catch(...) {
		// Logging must never throw.
	}
}

log_capture::log_capture() : previous(current_capture), installed(true) {
	current_capture = this;
}

void log_capture::restore() {
	if(installed) {
		current_capture = previous;
		installed = false;
	}
}

void log_capture::flush() {
	restore();
	std::vector<message> captured;
	captured.swap(messages);
	for(size_t i = 0; i < captured.size(); i++) {
		logger::emit(captured[i].level, captured[i].text);
	}
}

bool log_capture::has_problems() const {
	for(size_t i = 0; i < messages.size(); i++) {
		if(messages[i].level >= logger::Warning) {
			return true;
		}
	}
	return false;
}
