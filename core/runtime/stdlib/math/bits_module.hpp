#ifndef LUMA_STDLIB_BITS_MODULE_HPP
#define LUMA_STDLIB_BITS_MODULE_HPP

#include <memory>

namespace luma {

class Environment;
using EnvPtr = std::shared_ptr<Environment>;

// Registers the `Bits` module — integer bit manipulation as pipe-first free
// functions (Bits.and, Bits.or, Bits.xor, Bits.not, Bits.shift_left,
// Bits.shift_right).  These replace the former `& | ^ ~ << >>` operators, which
// were removed from the language surface (R06).
void register_bits_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_BITS_MODULE_HPP
