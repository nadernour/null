#pragma once

#if defined(NE_BUILD_SHIPPING)
#define NE_BREAK() ((void)0) // No debugger to break into
#elif defined(NE_PLATFORM_WINDOWS)
#define NE_BREAK() __debugbreak()
#elif (defined(NE_PLATFORM_LINUX) || defined(NE_PLATFORM_APPLE)) && (defined(__GNUC__) || defined(__clang__))
#define NE_BREAK() __builtin_trap()
#else
#include <cstdlib>
#define NE_BREAK() std::abort()
#endif
