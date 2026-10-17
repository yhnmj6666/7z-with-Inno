// minihead.cpp
// Minimal stand-ins for the Boost facilities used by the innoextract-derived code
// (written for this plugin, not part of innoextract). Some parts are adapted from
// Boost.Iostreams and Boost.Integer (Boost Software License 1.0, see
// THIRD-PARTY-NOTICES.md).

#include "minihead.hpp"

namespace boost {

std::string to_lower_copy(const std::string& str)
{
	std::string res = std::string(str);
	for (size_t i = 0; i < res.length(); i++)
	{
		res[i] = (char)tolower(res[i]);
	}
	return res;
}

bool contains(const ::std::string& left, const char* right)
{
	if (left.find(right) != ::std::string::npos)
	{
		return true;
	}
	return false;
}

bool iequals(const::std::string& left, const::std::string& right)
{
	return to_lower_copy(left) == to_lower_copy(right);
}

template<>
unsigned int lexical_cast<unsigned int>(const char* str, size_t count)
{
	return ::std::stoul(::std::string(str, count));
}

namespace algorithm {
decltype(::boost::to_lower_copy)* to_lower_copy = ::boost::to_lower_copy;
}

namespace posix_time {
duration operator-(const ptime& l, const ptime& r)
{
	return duration((::std::chrono::steady_clock::time_point)l - (::std::chrono::steady_clock::time_point)r);
}
}

}
