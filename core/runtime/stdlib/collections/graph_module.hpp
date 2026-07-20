#ifndef LUMA_STDLIB_GRAPH_MODULE_HPP
#define LUMA_STDLIB_GRAPH_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_graph_ns(const EnvPtr& env);

// Internal sub-registration function (split for readability).
void register_graph_algorithms(const EnvPtr& env);

// Sub-registrations of register_graph_algorithms, split by algorithm family into
// graph_traversal.cpp, graph_paths.cpp, and graph_ordering.cpp.
void register_graph_traversal(const EnvPtr& env);
void register_graph_paths(const EnvPtr& env);
void register_graph_ordering(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_GRAPH_MODULE_HPP
