#ifndef LUMA_COMMON_UNREACHABLE_HPP
#define LUMA_COMMON_UNREACHABLE_HPP

// Marks a code path as unreachable for the optimizer.
// In debug builds, this is a hard abort; in release builds,
// it is a hint to the compiler that the path is never taken.
#ifdef NDEBUG
#ifdef _MSC_VER
#define LUMA_UNREACHABLE() __assume(false)
#else
#define LUMA_UNREACHABLE() __builtin_unreachable()
#endif
#else
#include <cstdlib>
#define LUMA_UNREACHABLE() std::abort()
#endif

#endif // LUMA_COMMON_UNREACHABLE_HPP
