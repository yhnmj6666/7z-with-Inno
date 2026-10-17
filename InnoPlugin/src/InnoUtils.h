#pragma once

#ifndef __INNO_UTIL_H
#define __INNO_UTIL_H

#include <string>

namespace NArchive {
namespace NInno {
class state3
{
	enum _state3 { unsure, yes, no };
	int _;
public:
	state3()
	{
		_ = unsure;
	}
	state3 operator=(bool value)
	{
		if (_ == unsure)
			_ = value ? yes : no;
		else if (_ == yes && value == false)
			_ = unsure;
		else if (_ == no && value == true)
			_ = unsure;
		return *this;
	}
	operator bool()
	{
		return _ == yes;
	}
};

std::string ReplaceString(std::string subject, const std::string& search,
	const std::string& replace);
}
}
#endif