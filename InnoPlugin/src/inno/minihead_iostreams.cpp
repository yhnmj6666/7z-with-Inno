// minihead_iostreams.cpp
// Reimplements the parts of the Boost interface that the innoextract-derived
// code uses (written for this plugin, not part of innoextract).

#include "minihead_iostreams.hpp"

namespace boost {
namespace iostreams {

void filtering_istream::exceptions(::std::ios_base::iostate state)
{
	::std::istream::exceptions(state);
}

}
}
