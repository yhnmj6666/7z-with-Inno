// Origin: innoextract src/util/console.hpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: the plugin never writes to a terminal, so
// only the color commands remain, and they are empty.
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
 * Terminal colors (always empty in the plugin).
 */
#ifndef INNOEXTRACT_UTIL_CONSOLE_HPP
#define INNOEXTRACT_UTIL_CONSOLE_HPP

#include <ostream>

namespace color {

struct shell_command {
	const char * command;
};

//! Reset the output color to the original value.
extern const shell_command reset;

extern const shell_command black;
extern const shell_command red;
extern const shell_command green;
extern const shell_command yellow;
extern const shell_command blue;
extern const shell_command magenta;
extern const shell_command cyan;
extern const shell_command white;

extern const shell_command dim_black;
extern const shell_command dim_red;
extern const shell_command dim_green;
extern const shell_command dim_yellow;
extern const shell_command dim_blue;
extern const shell_command dim_magenta;
extern const shell_command dim_cyan;
extern const shell_command dim_white;

//! The last set output color (always \ref reset).
extern const shell_command current;

inline std::ostream & operator<<(std::ostream & os, shell_command command) {
	return os << command.command;
}

} // namespace color

#endif // INNOEXTRACT_UTIL_CONSOLE_HPP
