// minihead_iostreams.cpp
// Minimal stand-ins for the Boost facilities used by the innoextract-derived code
// (written for this plugin, not part of innoextract). Some parts are adapted from
// Boost.Iostreams and Boost.Integer (Boost Software License 1.0, see
// THIRD-PARTY-NOTICES.md).

#include "minihead_iostreams.hpp"

namespace boost {
namespace iostreams {

void filtering_istream::exceptions(::std::ios_base::iostate state)
{
	::std::istream::exceptions(state);
}

}
}
