// Origin: innoextract src/util/log.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: messages never go to the console; they
// are collected per thread (log_capture) or written to an opt-in debug log.
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
 * Logging functions.
 */
#ifndef INNOEXTRACT_UTIL_LOG_HPP
#define INNOEXTRACT_UTIL_LOG_HPP

#include <sstream>
#include <string>
#include <vector>

#define debug(...) \
	if(::logger::debug_enabled()) \
		::logger(::logger::Debug) << __VA_ARGS__

#define log_info    ::logger(::logger::Info)
#define log_warning ::logger(::logger::Warning)
#define log_error   ::logger(::logger::Error)

/*!
 * logger class that allows logging via the stream operator.
 *
 * Nothing is ever written to the console. A finished message goes to the innermost
 * \ref log_capture of the current thread. Without a capture, it is appended to the
 * debug log file named by the INNO_PLUGIN_LOG environment variable, if that is set.
 */
class logger : private boost::noncopyable {
	
public:
	
	enum log_level {
		Debug,
		Info,
		Warning,
		Error
	};
	
private:
	
	const log_level level;
	
	std::ostringstream buffer; //! Buffer for the log message excluding level, file and line.
	
public:
	
	/*!
	 * Construct a log line output stream.
	 *
	 * You probably don't want to use this directly - use \ref debug, \ref log_info,
	 * \ref log_warning and \ref log_error instead.
	 */
	explicit logger(log_level _level) : level(_level) { }
	
	template <class T>
	logger & operator<<(const T & i) {
		buffer << i;
		return *this;
	}
	
	~logger();
	
	//! Is the opt-in debug log (INNO_PLUGIN_LOG) enabled?
	static bool debug_enabled();
	
	//! Pass a finished message to the current capture or to the debug log.
	static void emit(log_level level, const std::string & message);
	
};

/*!
 * Collects the messages logged by the current thread while it is installed.
 *
 * Captures nest: the constructor installs the capture, \ref restore() and
 * \ref flush() uninstall it again.
 */
class log_capture : private boost::noncopyable {
	
public:
	
	struct message {
		logger::log_level level;
		std::string text;
	};
	
private:
	
	log_capture * previous;
	bool installed;
	std::vector<message> messages;
	
	friend class logger;
	
public:
	
	log_capture();
	
	~log_capture() {
		restore();
	}
	
	//! Stop capturing messages; captured messages are kept but not passed on.
	void restore();
	
	//! Stop capturing and pass the captured messages to the enclosing capture or the debug log.
	void flush();
	
	//! Were any warnings or errors captured?
	bool has_problems() const;
	
	const std::vector<message> & get() const { return messages; }
	
};

//! Captures warnings while trying to parse the headers with a candidate version.
class warning_suppressor : public log_capture {
	
public:
	
	operator bool() const {
		return has_problems();
	}
	
};

#endif // INNOEXTRACT_UTIL_LOG_HPP
