#ifndef LUMA_COMMON_EXPORT_HPP
#define LUMA_COMMON_EXPORT_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Library Export Macros
// ─────────────────────────────────────────────────────────────────────────────
// When building luma_core as a shared library (LUMA_BUILD_SHARED=ON),
// public API symbols must be annotated with LUMA_API.
//
// Usage:
//   class LUMA_API VM { ... };
//   LUMA_API Value interpret(const std::string& source);
//
// When building as static, LUMA_API expands to nothing.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(LUMA_SHARED)
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(LUMA_BUILDING_SHARED)
#define LUMA_API __declspec(dllexport)
#else
#define LUMA_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define LUMA_API __attribute__((visibility("default")))
#else
#define LUMA_API
#endif
#else
#define LUMA_API
#endif

#endif // LUMA_COMMON_EXPORT_HPP
