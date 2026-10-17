// Origin: innoextract src/util/console.cpp, commit 83c1544b3f (master, 2021-12-02).
// Modified for the 7-Zip Inno plugin: the plugin never writes to a terminal, so
// only the color commands remain, and they are empty.
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

#include "../util/console.hpp"

namespace color {

const shell_command black =       { "" };
const shell_command red =         { "" };
const shell_command green =       { "" };
const shell_command yellow =      { "" };
const shell_command blue =        { "" };
const shell_command magenta =     { "" };
const shell_command cyan =        { "" };
const shell_command white =       { "" };

const shell_command dim_black =   { "" };
const shell_command dim_red =     { "" };
const shell_command dim_green =   { "" };
const shell_command dim_yellow =  { "" };
const shell_command dim_blue =    { "" };
const shell_command dim_magenta = { "" };
const shell_command dim_cyan =    { "" };
const shell_command dim_white =   { "" };

const shell_command reset =       { "" };

const shell_command current =     { "" };

} // namespace color
