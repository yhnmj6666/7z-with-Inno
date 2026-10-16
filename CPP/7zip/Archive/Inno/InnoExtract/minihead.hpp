// minihead.hpp
#pragma once
#ifndef _MINIHEAD_HPP
#define _MINIHEAD_HPP

#include "../../../../../C/7zCrc.h"

//#define NOMINMAX
#define LZMA_API_STATIC
#define INNOEXTRACT_HAVE_STD_CODECVT_UTF8_UTF16 1
#define INNOEXTRACT_HAVE_STD_UNIQUE_PTR 1
#define INNOEXTRACT_HAVE_LZMA 1
#define INNOEXTRACT_HAVE_ARC4 1

#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <filesystem>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>

#define minihead_log_func(sig) ::std::cerr<<(##sig)<<::std::endl


#define BOOST_VERSION 999999
namespace boost {
#define BOOST_STATIC_ASSERT(...) static_assert(__VA_ARGS__, #__VA_ARGS__)
#define BOOST_FOREACH(x,y) for(x : y)
#define BOOST_TYPEOF(expr) boost::type_of::remove_cv_ref_t<decltype(expr)>
using int8_t = ::int8_t;
using uint8_t = ::uint8_t;
using int16_t = ::int16_t;
using uint16_t = ::uint16_t;
using int32_t = ::int32_t;
using uint32_t = ::uint32_t;
using uint_fast32_t = ::uint32_t;
using int64_t = ::int64_t;
using uint64_t = ::uint64_t;

class bad_pointer : public ::std::exception
{

};

class bad_lexical_cast : public ::std::exception
{

};

template <bool B, class T = void>
struct enable_if_c {
	typedef T type;
};

template <class T>
struct enable_if_c<false, T> {};

template<class C1, class C2>
using unordered_map = ::std::unordered_map<C1,C2>;

template<class C>
using scoped_ptr = ::std::unique_ptr<C>;

template<class C1, class C2>
class ptr_map : public ::std::map<C1, C2*>
{
public:
	inline void insert(C1 key, C2* value) {
		const auto it = this->find(key);
		this->insert(it, value);
	}
};

template<class C>
using ptr_vector = ::std::vector<C*>;

typedef unsigned long long uintmax_t;
template <uintmax_t Value1, uintmax_t Value2>
struct static_unsigned_min
{
	static const uintmax_t value = (Value1 > Value2) ? Value2 : Value1;
};

template<class T>
inline
size_t size(const T& rng)
{
	return std::size(rng);
}

template<class T>
inline
T lexical_cast(const char* str, size_t count)
{
	return T();
}

template<>
unsigned int lexical_cast<unsigned int>(const char* str, size_t count);

inline std::string to_lower_copy(const std::string& str);

template< class T, ::std::size_t N >
inline
constexpr T* begin(T(&array)[N])
{
	return ::std::begin<T, N>(array);
}

template< class T, ::std::size_t N >
inline
constexpr T* end(T(&array)[N])
{
	return ::std::end<T, N>(array);
}

bool contains(const ::std::string& left, const char* right);
bool iequals(const ::std::string& left, const ::std::string& right);

namespace algorithm {
extern decltype(::boost::to_lower_copy)* to_lower_copy;
}

namespace container {
template<class C1, class C2>
using flat_map = ::std::map<C1, C2>;
}

namespace filesystem {

using namespace std::filesystem;
using ifstream = std::ifstream;
using ofstream = std::ofstream;
using fstream = std::fstream;
}

namespace posix_time {
class duration : public ::std::chrono::duration<double>
{
public: 
	uint64_t total_microseconds()
	{
		return ::std::chrono::duration_cast<std::chrono::microseconds>(*this).count();
	}
};

class ptime : public ::std::chrono::steady_clock::time_point {
public:
	ptime() : ::std::chrono::steady_clock::time_point() {}
	ptime(::std::chrono::steady_clock::time_point) {}
	inline static ptime universal_time()
	{
		return ::std::chrono::high_resolution_clock::now();
	}
};

duration operator-(const ptime& l, const ptime& r);

using microsec_clock = ptime;

}

class noncopyable {};

template <int Bits>
struct exact_signed_base_helper {};
template <int Bits>
struct exact_unsigned_base_helper {};

template <> struct exact_signed_base_helper<sizeof(signed char)* CHAR_BIT> { typedef signed char exact; };
template <> struct exact_unsigned_base_helper<sizeof(unsigned char)* CHAR_BIT> { typedef unsigned char exact; };
#if USHRT_MAX != UCHAR_MAX
template <> struct exact_signed_base_helper<sizeof(short)* CHAR_BIT> { typedef short exact; };
template <> struct exact_unsigned_base_helper<sizeof(unsigned short)* CHAR_BIT> { typedef unsigned short exact; };
#endif
#if UINT_MAX != USHRT_MAX
template <> struct exact_signed_base_helper<sizeof(int)* CHAR_BIT> { typedef int exact; };
template <> struct exact_unsigned_base_helper<sizeof(unsigned int)* CHAR_BIT> { typedef unsigned int exact; };
#endif
#if ULONG_MAX != UINT_MAX
template <> struct exact_signed_base_helper<sizeof(long)* CHAR_BIT> { typedef long exact; };
template <> struct exact_unsigned_base_helper<sizeof(unsigned long)* CHAR_BIT> { typedef unsigned long exact; };
#endif
#if ((defined(ULLONG_MAX) && (ULLONG_MAX != ULONG_MAX)) ||\
    (defined(ULONG_LONG_MAX) && (ULONG_LONG_MAX != ULONG_MAX)) ||\
    (defined(ULONGLONG_MAX) && (ULONGLONG_MAX != ULONG_MAX)) ||\
    (defined(_ULLONG_MAX) && (_ULLONG_MAX != ULONG_MAX)))
template <> struct exact_signed_base_helper<sizeof(long long)* CHAR_BIT> { typedef long long exact; };
template <> struct exact_unsigned_base_helper<sizeof(unsigned long long)* CHAR_BIT> { typedef unsigned long long exact; };
#endif

//  signed
template< int Bits >   // bits (including sign) required
struct int_t : public exact_signed_base_helper<Bits>
{
};

//  unsigned
template< int Bits >   // bits required
struct uint_t : public exact_unsigned_base_helper<Bits>
{
};

namespace type_of {
template<typename T>
	using remove_cv_ref_t = T;
}

}

#include "minihead_iostreams.hpp"

#endif