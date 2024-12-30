// Copyright 2015, 2016 by Martin Moene
//
// nonstd::observer_ptr<> is a C++98 onward implementation for std::observer_ptr as of C++17.
//
// This code is licensed under the MIT License (MIT).
//

#pragma once

#ifndef NONSTD_OBSERVER_PTR_H_INCLUDED
#define NONSTD_OBSERVER_PTR_H_INCLUDED

#include <cassert>
#include <functional>
#include <memory>

#define  observer_ptr_VERSION "0.2.0"

// Configuration:

#ifndef  nop_FEATURE_ALLOW_IMPLICIT_CONVERSION
# define nop_FEATURE_ALLOW_IMPLICIT_CONVERSION  0
#endif

#ifndef  nop_CONFIG_CONFIRMS_COMPILATION_ERRORS
# define nop_CONFIG_CONFIRMS_COMPILATION_ERRORS  0
#endif

// Compiler detection:

#define nop_CPP11_OR_GREATER  ( __cplusplus >= 201103L )
#define nop_CPP14_OR_GREATER  ( __cplusplus >= 201402L )

// half-open range [lo..hi):
#define nop_BETWEEN( v, lo, hi ) ( lo <= v && v < hi )

#if defined(_MSC_VER)
# define nop_COMPILER_MSVC_VERSION   (_MSC_VER / 100 - 5 - (_MSC_VER < 1900))
#else
# define nop_COMPILER_MSVC_VERSION   0
# define nop_COMPILER_NON_MSVC       1
#endif

// Presence of C++ language features:

#if nop_CPP11_OR_GREATER
# define nop_HAVE_CONSTEXPR_11  1
#endif

#if nop_CPP14_OR_GREATER
# define nop_HAVE_CONSTEXPR_14  1
#endif

#if nop_CPP11_OR_GREATER || nop_COMPILER_MSVC_VERSION >= 14
# define nop_HAVE_EXPLICIT_CONVERSION  1
#endif

#if nop_CPP11_OR_GREATER || nop_COMPILER_MSVC_VERSION >= 14
# define nop_HAVE_NOEXCEPT  1
#endif

#if nop_CPP11_OR_GREATER || nop_COMPILER_MSVC_VERSION >= 10
# define nop_HAVE_NULLPTR  1
#endif

#if defined( __GNUC__ )
# define nop_HAVE_TYPEOF  1
#endif

// Presence of C++ library features:

// For the rest, consider VC12, VC14 as C++11 for observer_ptr<>:

#if nop_COMPILER_MSVC_VERSION >= 12
# undef  nop_CPP11_OR_GREATER
# define nop_CPP11_OR_GREATER  1
#endif

#if nop_CPP11_OR_GREATER || nop_COMPILER_MSVC_VERSION >= 11
# define nop_HAVE_STD_DECAY    1
# define nop_HAVE_STD_DECLVAL  1
#endif

// C++ feature usage:

#if nop_HAVE_CONSTEXPR_11
# define nop_constexpr constexpr
#else
# define nop_constexpr /*nothing*/
#endif

#if nop_HAVE_CONSTEXPR_14
# define nop_constexpr14 constexpr
#else
# define nop_constexpr14 /*nothing*/
#endif

#if nop_HAVE_EXPLICIT_CONVERSION
# define nop_explicit explicit
#else
# define nop_explicit /*nothing*/
#endif

#if nop_HAVE_NOEXCEPT
# define nop_noexcept noexcept
#else
# define nop_noexcept /*nothing*/
#endif

#if nop_HAVE_NULLPTR
# define nop_NULLPTR nullptr
#else
# define nop_NULLPTR NULL
#endif

// common_type:

#if nop_HAVE_STD_DECAY && nop_HAVE_STD_DECLVAL
# include <type_traits>  // std::decay
# include <utility>      // std::declval
# define nop_HAVE_OWN_COMMON_TYPE      1
# define nop_HAVE_OWN_COMMON_TYPE_STD  1
#elif nop_HAVE_TYPEOF
# define nop_HAVE_OWN_COMMON_TYPE         1
# define nop_HAVE_OWN_COMMON_TYPE_TYPEOF  1
#endif

namespace nonstd
{

template< class W >
class observer_ptr
{
public:
    typedef W   element_type;
    typedef W * pointer;
    typedef W & reference;

    nop_constexpr14 observer_ptr() nop_noexcept
    : ptr( nop_NULLPTR ) {}

#if nop_HAVE_NULLPTR
    nop_constexpr14 observer_ptr( std::nullptr_t ) nop_noexcept
    : ptr( nullptr ) {}
#endif

    nop_constexpr14 observer_ptr( pointer p ) nop_noexcept
    : ptr(p) {}

    template< class W2 >
    nop_constexpr14 observer_ptr(observer_ptr<W2> other ) nop_noexcept
    : ptr( other.get() ) {}

    template< class W2 >
    nop_constexpr14 observer_ptr(const std::unique_ptr<W2>& other)
    : ptr(other.get()) {}

    template< class W2 >
    nop_constexpr14 observer_ptr(const std::shared_ptr<W2>& other)
    : ptr(other.get()) {}

    nop_constexpr14 pointer get() const nop_noexcept
    {
        return ptr;
    }

    nop_constexpr14 reference operator*() const
    {
        return assert( ptr != nop_NULLPTR ), *ptr;
    }

    nop_constexpr14 pointer operator->() const nop_noexcept
    {
        return ptr;
    }

#if nop_HAVE_EXPLICIT_CONVERSION

    nop_constexpr14 explicit operator bool() const nop_noexcept
    {
        return ptr != nop_NULLPTR;
    }

    nop_constexpr14 explicit operator pointer() const nop_noexcept
    {
        return ptr;
    }
#elif nop_FEATURE_ALLOW_IMPLICIT_CONVERSION

    nop_constexpr14 operator pointer() const nop_noexcept
    {
        return ptr;
    }
#else
    typedef void (observer_ptr::*safe_bool)() const;
    void this_type_does_not_support_comparisons() const {}

    nop_constexpr14 operator safe_bool() const nop_noexcept
    {
        return ptr != nop_NULLPTR ? &observer_ptr::this_type_does_not_support_comparisons : 0;
    }
#endif

    nop_constexpr14 pointer release() nop_noexcept
    {
        pointer p( ptr );
        reset();
        return p;
    }

    nop_constexpr14 void reset( pointer p = nop_NULLPTR ) nop_noexcept
    {
        ptr = p;
    }

    nop_constexpr14 void swap( observer_ptr & other ) nop_noexcept
    {
        using std::swap;
        swap(ptr, other.ptr);
    }

private:
    pointer ptr;
};

// specialized algorithms:

template< class W >
void swap( observer_ptr<W> & p1, observer_ptr<W> & p2 ) nop_noexcept
{
    p1.swap( p2 );
}

template< class W >
observer_ptr<W> make_observer( W * p ) nop_noexcept
{
    return observer_ptr<W>( p );
}

template< class W1, class W2 >
bool operator==( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    return p1.get() == p2.get();
}

template< class W1, class W2 >
bool operator!=( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    return !( p1 == p2 );
}

#if nop_HAVE_NULLPTR

template< class W >
bool operator==( observer_ptr<W> p, std::nullptr_t ) nop_noexcept
{
    return !p;
}

template< class W >
bool operator==( std::nullptr_t, observer_ptr<W> p ) nop_noexcept
{
    return !p;
}

template< class W >
bool operator!=( observer_ptr<W> p, std::nullptr_t ) nop_noexcept
{
    return (bool)p;
}

template< class W >
bool operator!=( std::nullptr_t, observer_ptr<W> p ) nop_noexcept
{
    return (bool)p;
}
#endif

namespace detail
{
    template< class T, class U >
#if nop_HAVE_OWN_COMMON_TYPE_STD
    struct common_type { typedef typename std::decay< decltype(true ? std::declval<T>() : std::declval<U>()) >::type type; };
#elif nop_HAVE_OWN_COMMON_TYPE_TYPEOF
    struct common_type { typedef __typeof__( true ? T() : U() ) type; };
#else // fall back
    struct common_type { typedef T type; };
#endif
} // namespace detail

template< class W1, class W2 >
bool operator<( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    // return std::less<W3>()( p1.get(), p2.get() );
    // where W3 is the composite pointer type (C++14 Section 5) of W1* and W2*.
    return std::less< typename detail::common_type<W1*,W2*>::type >()( p1.get(), p2.get() );
}

template< class W1, class W2 >
bool operator>( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    return p2 < p1;
}

template< class W1, class W2 >
bool operator<=( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    return !( p2 < p1 );
}

template< class W1, class W2 >
bool operator>=( observer_ptr<W1> p1, observer_ptr<W2> p2 )
{
    return !( p1 < p2 );
}

} // namespace nonstd

// #undef ...

#endif // NONSTD_OBSERVER_PTR_H_INCLUDED

// end of file
