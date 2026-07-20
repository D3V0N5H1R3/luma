// BinaryTree module registration.
//
// BinaryTree does not use ContainerModuleBuilder because its data is stored as a
// recursive tree structure (BinaryTreeNode with left/right children), not a
// contiguous `std::vector<Value> elements`.  The registrations are grouped into
// two cohesive sub-functions, each in its own translation unit: core operations
// and advanced (search/set/transform) operations.

#include "runtime/stdlib/collections/binarytree_module.hpp"

namespace luma {

void register_binarytree_ns(const EnvPtr& env) {
    register_binarytree_core(env);
    register_binarytree_advanced(env);
}

} // namespace luma
