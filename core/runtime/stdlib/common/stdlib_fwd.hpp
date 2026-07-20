#ifndef LUMA_STDLIB_FWD_HPP
#define LUMA_STDLIB_FWD_HPP

// ═══════════════════════════════════════════════════════════
// Shared forward declarations for stdlib module headers
// ═══════════════════════════════════════════════════════════
//
// All stdlib module .hpp files include this header to obtain
// the Environment forward declaration and the EnvPtr alias,
// eliminating repetition across 40+ module headers.

#include <memory>

namespace luma {

class Environment;
using EnvPtr = std::shared_ptr<Environment>;

} // namespace luma

#endif // LUMA_STDLIB_FWD_HPP
