// ─────────────────────────────────────────────────────────────────────────────
// CompilerHelper — Base class for compiler helper classes
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Eliminate repeated boilerplate across compiler helper classes
//   that all hold a non-owning ICompilationBackend reference and forward to it.
//
// Design: Stores a protected ICompilationBackend& member and provides a single
//   explicit constructor.  Derived classes inherit the constructor via
//   `using CompilerHelper::CompilerHelper;` and access the API through `api_`.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILER_HELPER_HPP
#define LUMA_COMPILER_COMPILER_HELPER_HPP

#include "runtime/compiler/i_compilation_backend.hpp"

namespace luma {

// Base class for compiler helper classes that need access to the compiler API.
// Lifetime is bounded by the Compiler that owns each helper by value.
class CompilerHelper {
public:
    explicit CompilerHelper(ICompilationBackend& api) noexcept : api_(api) {}

protected:
    ICompilationBackend& api_;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILER_HELPER_HPP
