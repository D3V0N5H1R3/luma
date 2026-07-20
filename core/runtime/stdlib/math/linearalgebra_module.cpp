#include "runtime/stdlib/math/linearalgebra_module.hpp"

namespace luma {

void register_linearalgebra_ns(const EnvPtr& env) {
    register_linearalgebra_vectors(env);
    register_linearalgebra_matrices(env);
}

} // namespace luma
