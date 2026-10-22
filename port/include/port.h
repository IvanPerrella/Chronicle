#pragma once

#include <strings.h>

#if defined(__ANDROID__) && !defined(__ANDROID_ASSERT_RENAME)
#define __ANDROID_ASSERT_RENAME
#define __assert __android_libc_assert
#endif
#include <cassert>
#include <cstddef>
#include <new>
#ifdef __ANDROID__
#undef __assert
#undef __ANDROID_ASSERT_RENAME
#endif
#ifdef __APPLE__
#undef __assert
#endif
#ifdef __APPLE__
#define section(name)
#endif
#ifdef __PSP__
// newlib's static libc defines __assert beside __assert_func, which the host code's assert() links in.
#define __assert dc_assert
#endif

#define PORT_STRINGIFY2(x) #x
#define PORT_STRINGIFY(x) PORT_STRINGIFY2(x)
#define PORT_ASM_NAME(name) PORT_STRINGIFY(__USER_LABEL_PREFIX__) #name

#include "kazarov/port.h"

/**
 * The Metrowerks runtime's assertion failure.
 */
#if defined(__ANDROID__) || defined(__PSP__)
extern "C" [[noreturn]] void __assert(const char *file, int line, const char *expression);
#else
[[noreturn]] void __assert(const char *file, int line, const char *expression);
#endif

/**
 * The Metrowerks runtime's exit.
 */
extern "C" [[noreturn]] void exit__2(int status);

/**
 * Makes a temporary an lvalue, for the non-const reference parameters MWCC binds temporaries to.
 */
template <class T>
T &Ps2Lvalue(T &&value) {
    return static_cast<T &>(value);
}

// types.h gives the PS2's size_t and NULL. Included here under another name,
// the host's stay in force, and #pragma once keeps the game from including it again.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#ifdef __PSP__
// newlib's BSD u_long must be declared before the rename below.
#include <sys/types.h>
#endif
#pragma push_macro("NULL")
#define size_t ps2_size_t
#if defined(_WIN32) || defined(__PSP__)
// Windows uses LLP64 and the PSP's EABI a 32-bit long; the GS register bitfields require the PS2's 64-bit u_long.
#define u_long ps2_native_u_long
#endif
#ifdef __PSP__
// 32-bit MIPS has no __int128; u_long128 is the 16-byte block defined below.
#define u_long128 ps2_native_u_long128
#define __int128 int
#endif
#include "types.h"
#ifdef __PSP__
#undef __int128
#undef u_long128
#endif
#if defined(_WIN32) || defined(__PSP__)
#undef u_long
#define u_long u64
#endif
#undef size_t
#pragma pop_macro("NULL")
#pragma clang diagnostic pop

#ifdef __PSP__
// The game only stores, copies and points at quadwords; it does no 128-bit arithmetic.
struct alignas(16) u_long128 {
    u64 lo, hi;
    u_long128() = default;
    constexpr u_long128(u64 value) : lo(value), hi(0) {}
};
#endif

// The layouts STATIC_ASSERT checks are the PS2's: 32-bit pointers and longs as
// MWCC lays them out. Once common.h is in, it checks nothing.
#include "common.h"
#undef STATIC_ASSERT
#define STATIC_ASSERT(expr)
