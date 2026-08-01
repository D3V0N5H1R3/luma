#ifndef LUMA_DAP_VALUE_EXPANDER_HPP
#define LUMA_DAP_VALUE_EXPANDER_HPP

#include <string>
#include <vector>

#include "dap_types.hpp"

namespace luma {
class Value;
} // namespace luma

namespace luma::dap {

class VariableInspector;

// ═══════════════════════════════════════════════════════════
// ValueExpander — expands a structured runtime Value into its
// child DAP Variables.
//
// Extracted from VariableInspector so that the per-collection-type
// expansion logic lives apart from reference/scope lifecycle and
// mutation.  Holds a non-owning reference to the owning inspector and
// calls back into its public make_variable() to build (and lazily
// ref-allocate) each child Variable.
// ═══════════════════════════════════════════════════════════
class ValueExpander {
public:
    explicit ValueExpander(const VariableInspector& inspector) : inspector_(inspector) {}

    // Expand a structured Value into its child Variables, honouring DAP paging
    // (start/count) and the requested variable filter.  value_depth is the
    // nesting depth of the value itself; children are built at value_depth + 1.
    [[nodiscard]] std::vector<Variable> get_value_variables(const Value& val, int value_depth,
                                                            int start, int count,
                                                            const std::string& filter) const;

private:
    // Per-type expanders.
    [[nodiscard]] std::vector<Variable> get_array_variables(const Value& val, int start, int count,
                                                            int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_tuple_variables(const Value& val, int start, int count,
                                                            int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_dictionary_variables(const Value& val,
                                                                 int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_record_variables(const Value& val,
                                                             int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_choice_variables(const Value& val,
                                                             int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_result_variables(const Value& val,
                                                             int child_depth) const;

    // Shared core for indexed collections (array-like): builds `[i]` children
    // over the given element range, honouring DAP start/count paging.
    [[nodiscard]] std::vector<Variable> get_indexed_variables(const std::vector<Value>& elements,
                                                              int start, int count,
                                                              int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_key_value_store_variables(const Value& val) const;
    [[nodiscard]] std::vector<Variable> get_xml_variables(const Value& val, int child_depth) const;
    [[nodiscard]] std::vector<Variable> get_range_variables(const Value& val) const;
    [[nodiscard]] std::vector<Variable> get_reference_variables(const Value& val,
                                                                int child_depth) const;

    const VariableInspector& inspector_;
};

} // namespace luma::dap

#endif // LUMA_DAP_VALUE_EXPANDER_HPP
