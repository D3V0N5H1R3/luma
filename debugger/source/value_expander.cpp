#include "value_expander.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/narrow_int.hpp"
#include "dap_types.hpp"
#include "runtime/interpreter/value.hpp"
#include "variable_inspector.hpp"

namespace luma::dap {

namespace {

// Returns true if the filter excludes variables of the given indexed-type classification.
[[nodiscard]] static bool filter_excludes(const std::string& filter,
                                          bool is_indexed_type) noexcept {
    if (filter.empty()) {
        return false;
    }
    return (filter == "indexed" && !is_indexed_type) || (filter == "named" && is_indexed_type);
}

// Half-open [begin, end) element window for DAP start/count paging.
struct ElementWindow {
    int begin;
    int end;
};

// Compute the [begin, end) window into a `total`-element collection for the
// given DAP paging request.  start <= 0 means "from the beginning"; count <= 0
// means "through the end".
[[nodiscard]] static ElementWindow compute_window(int start, int count, int total) noexcept {
    const int begin = (start > 0) ? std::min(start, total) : 0;
    const int end = (count > 0) ? begin + std::min(count, total - begin) : total;
    return {begin, end};
}

// Build a leaf Variable directly from pre-rendered strings.  Unlike
// make_base_variable (which formats a Value), these leaves carry raw text such
// as a choice tag, a stringified range bound, or a boolean flag.
[[nodiscard]] static Variable make_leaf_variable(std::string name, std::string value,
                                                 std::string type, std::string evaluate_name = "") {
    Variable var;
    var.name = std::move(name);
    var.value = std::move(value);
    var.type = std::move(type);
    var.evaluate_name = std::move(evaluate_name);
    return var;
}

// Render a boolean as the lowercase literal used in variable displays.
[[nodiscard]] static std::string bool_to_string(bool value) {
    return value ? "true" : "false";
}

// Human-readable name for an XML node's type, used as the leaf value of an
// XML node's `node_type` child in the variables view.
[[nodiscard]] static std::string xml_node_type_name(XmlValue::NodeType type) {
    switch (type) {
        case XmlValue::NodeType::Element:
            return "element";
        case XmlValue::NodeType::Text:
            return "text";
        case XmlValue::NodeType::Comment:
            return "comment";
        case XmlValue::NodeType::CData:
            return "cdata";
    }
    return "element";
}

} // namespace

std::vector<Variable> ValueExpander::get_value_variables(const Value& val, int value_depth,
                                                         int start, int count,
                                                         const std::string& filter) const {
    const int child_depth = value_depth + 1;

    const auto classification = classify_value(val);

    if (filter_excludes(filter, classification.child_kind == ChildVariableKind::Indexed)) {
        return {};
    }

    switch (classification.kind) {
        case ValueKind::Array:
            return get_array_variables(val, start, count, child_depth);
        case ValueKind::Dictionary:
            return get_dictionary_variables(val, child_depth);
        case ValueKind::Tuple:
            return get_tuple_variables(val, start, count, child_depth);
        case ValueKind::Record:
            return get_record_variables(val, child_depth);
        case ValueKind::Choice:
            return get_choice_variables(val, child_depth);
        case ValueKind::Result:
            return get_result_variables(val, child_depth);
        case ValueKind::Queue:
            return get_indexed_variables(val.as_queue()->elements, start, count, child_depth);
        case ValueKind::Stack:
            return get_indexed_variables(val.as_stack()->elements, start, count, child_depth);
        case ValueKind::Set:
            return get_indexed_variables(val.as_set()->elements, start, count, child_depth);
        case ValueKind::BinaryTree:
            return get_binary_tree_variables(val, start, count, child_depth);
        case ValueKind::KeyValueStore:
            return get_key_value_store_variables(val);
        case ValueKind::Xml:
            return get_xml_variables(val, child_depth);
        case ValueKind::Range:
            return get_range_variables(val);
        case ValueKind::Reference:
            return get_reference_variables(val, child_depth);
        case ValueKind::Scalar:
            return {};
    }

    return {};
}

std::vector<Variable> ValueExpander::get_array_variables(const Value& val, int start, int count,
                                                         int child_depth) const {
    return get_indexed_variables(*val.as_array()->elements, start, count, child_depth);
}

std::vector<Variable> ValueExpander::get_tuple_variables(const Value& val, int start, int count,
                                                         int child_depth) const {
    std::vector<Variable> result;
    const auto& elements = val.as_tuple()->elements;
    const auto [begin, end] = compute_window(start, count, clamp_to_int(elements.size()));

    for (int i = begin; i < end; ++i) {
        auto var = inspector_.make_variable(
            std::format(".{}", i), elements[static_cast<std::size_t>(i)], false, child_depth);
        var.evaluate_name = std::format(".{}", i);
        result.push_back(std::move(var));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_dictionary_variables(const Value& val,
                                                              int child_depth) const {
    std::vector<Variable> result;

    for (const auto& [key, value] : val.as_dictionary()->entries) {
        auto var = inspector_.make_variable(key, value, false, child_depth);
        var.evaluate_name = std::format("[\"{}\"]", key);
        result.push_back(std::move(var));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_record_variables(const Value& val, int child_depth) const {
    std::vector<Variable> result;

    for (const auto& [name, value] : val.as_record()->fields) {
        auto var = inspector_.make_variable(name, value, false, child_depth);
        var.evaluate_name = name;
        result.push_back(std::move(var));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_choice_variables(const Value& val, int child_depth) const {
    std::vector<Variable> result;
    const auto& choice = val.as_choice();

    result.push_back(make_leaf_variable("variant", choice->variant, "string"));

    for (std::size_t i = 0; i < choice->fields.size(); ++i) {
        result.push_back(
            inspector_.make_variable(std::format(".{}", i), choice->fields[i], false, child_depth));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_result_variables(const Value& val, int child_depth) const {
    std::vector<Variable> result;
    const auto& res = val.as_result();

    result.push_back(make_leaf_variable("is_success", bool_to_string(res->is_success), "boolean"));

    if (res->owned_inner) {
        result.push_back(inspector_.make_variable("value", *res->owned_inner, false, child_depth));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_indexed_variables(const std::vector<Value>& elements,
                                                           int start, int count,
                                                           int child_depth) const {
    std::vector<Variable> result;
    const auto [begin, end] = compute_window(start, count, clamp_to_int(elements.size()));

    for (int i = begin; i < end; ++i) {
        auto var = inspector_.make_variable(
            std::format("[{}]", i), elements[static_cast<std::size_t>(i)], false, child_depth);
        var.evaluate_name = std::format("[{}]", i);
        result.push_back(std::move(var));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_binary_tree_variables(const Value& val, int start,
                                                               int count, int child_depth) const {
    // Iterative in-order traversal yields the BST's elements in sorted order
    // without risking native stack overflow on deep or degenerate trees.
    std::vector<Value> elements;
    elements.reserve(val.as_binary_tree()->size());

    std::vector<std::shared_ptr<BinaryTreeNode>> pending;
    auto node = val.as_binary_tree()->root;

    while (node || !pending.empty()) {
        while (node) {
            pending.push_back(node);
            node = node->left;
        }

        node = pending.back();
        pending.pop_back();
        elements.push_back(node->value);
        node = node->right;
    }

    return get_indexed_variables(elements, start, count, child_depth);
}

std::vector<Variable> ValueExpander::get_key_value_store_variables(const Value& val) const {
    std::vector<Variable> result;

    // KeyValueStore is a shared, concurrently-accessed collection guarded by its
    // own mutex (mirroring ReferenceValue::get()).  Under per-thread pausing,
    // another running task may still mutate the store, so hold the lock while
    // snapshotting its entries.
    const auto store = val.as_key_value_store();
    const std::lock_guard lock{store->mutex};

    for (const auto& [key, value] : store->entries) {
        result.push_back(make_leaf_variable(key, value, "string", std::format("[\"{}\"]", key)));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_xml_variables(const Value& val, int child_depth) const {
    std::vector<Variable> result;
    const auto& xml = *val.as_xml();

    result.push_back(make_leaf_variable("node_type", xml_node_type_name(xml.node_type), "string"));

    // Element nodes carry a tag name; text/comment/cdata nodes carry raw content.
    const bool is_element = xml.node_type == XmlValue::NodeType::Element;
    result.push_back(
        make_leaf_variable(is_element ? "tag" : "content", xml.tag_or_content, "string"));

    for (const auto& [name, value] : xml.attributes) {
        result.push_back(make_leaf_variable(std::format("@{}", name), value, "string"));
    }

    for (std::size_t i = 0; i < xml.children.size(); ++i) {
        result.push_back(inspector_.make_variable(std::format("[{}]", i), Value{xml.children[i]},
                                                  false, child_depth));
    }

    return result;
}

std::vector<Variable> ValueExpander::get_range_variables(const Value& val) const {
    const auto& range = *val.as_range();
    std::vector<Variable> result;

    result.push_back(make_leaf_variable("start", std::to_string(range.start), "integer"));
    result.push_back(make_leaf_variable("end", std::to_string(range.end), "integer"));
    result.push_back(make_leaf_variable("inclusive", bool_to_string(range.inclusive), "boolean"));

    return result;
}

std::vector<Variable> ValueExpander::get_reference_variables(const Value& val,
                                                             int child_depth) const {
    std::vector<Variable> result;
    result.push_back(
        inspector_.make_variable("value", val.as_reference()->get(), false, child_depth));
    return result;
}

} // namespace luma::dap
