#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <type_traits>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/recursion_guard.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/interpreter/value_collection_helpers.hpp"

// Value dispatch — uses switch(value_type()) for exhaustive dispatch.
// Omitting the default case ensures the compiler warns when a new
// ValueType enumerator is added without a corresponding copy case.
// See value_dispatch.hpp for dispatch conventions.

using luma::collection_helpers::deep_copy_elements_collection;

namespace {

// ─────────── deep_copy helpers ───────────

/// Deep-copy a vector of Values, preserving order.
[[nodiscard]] std::vector<luma::Value> deep_copy_values(const std::vector<luma::Value>& source) {
    std::vector<luma::Value> result;
    result.reserve(source.size());
    std::ranges::transform(source, std::back_inserter(result),
                           [](const luma::Value& v) { return v.deep_copy(); });
    return result;
}

[[nodiscard]] luma::Value deep_copy_record(const luma::RecordValue& record) {
    auto copy = std::make_shared<luma::RecordValue>();
    copy->type_name = record.type_name;

    std::ranges::transform(record.fields, std::back_inserter(copy->fields), [](const auto& field) {
        return std::pair{field.first, field.second.deep_copy()};
    });

    return luma::Value{std::move(copy)};
}

[[nodiscard]] luma::Value deep_copy_choice(const luma::ChoiceValue& choice) {
    auto copy = std::make_shared<luma::ChoiceValue>();
    copy->type_name = choice.type_name;
    copy->variant = choice.variant;
    copy->fields = deep_copy_values(choice.fields);

    return luma::Value{std::move(copy)};
}

[[nodiscard]] luma::Value deep_copy_dictionary(const luma::DictionaryValue& dictionary) {
    auto copy = std::make_shared<luma::DictionaryValue>();

    // The source keys are already unique, so copy the entries directly and let
    // the hash index rebuild lazily on first lookup. Going through set() would
    // rescan every prior entry to deduplicate on each insert, making a copy of
    // an unindexed dictionary quadratic in its size.
    copy->entries.reserve(dictionary.entries.size());
    std::ranges::transform(
        dictionary.entries, std::back_inserter(copy->entries),
        [](const auto& entry) { return std::pair{entry.first, entry.second.deep_copy()}; });

    return luma::Value{std::move(copy)};
}

[[nodiscard]] luma::Value deep_copy_result(const luma::ResultValue& result) {
    assert(result.owned_inner && "ResultValue.owned_inner must not be null");
    auto rv = std::make_shared<luma::ResultValue>();
    rv->is_success = result.is_success;
    rv->owned_inner = std::make_shared<luma::Value>(result.owned_inner->deep_copy());
    rv->has_failure_location = result.has_failure_location;
    rv->failure_location = result.failure_location;

    return luma::Value{std::move(rv)};
}

[[nodiscard]] luma::Value deep_copy_function(const luma::FunctionValue& func) {
    auto copy = std::make_shared<luma::FunctionValue>();
    copy->name = func.name;
    copy->compiled = func.compiled;
    copy->upvalues = deep_copy_values(func.upvalues);

    // Deep-copy mutable upvalue cells — spawned tasks get
    // independent copies so they don't share mutable state.
    std::ranges::transform(func.upvalue_cells, std::back_inserter(copy->upvalue_cells),
                           [](const auto& cell) -> std::shared_ptr<luma::Value> {
                               return cell ? std::make_shared<luma::Value>(cell->deep_copy())
                                           : nullptr;
                           });

    return luma::Value{std::move(copy)};
}

} // anonymous namespace

// Value dispatch — each value_*.cpp file follows the same pattern:
// switch on value_type() for exhaustive dispatch.  No default case —
// the compiler flags missing enumerators when a new type is added.
//
// Deep-copy semantics per group:
//   shallow          — Primitives, native functions, ranges, tasks,
//                      channels, sockets: trivially copyable, no
//                      recursion needed.
//   structural       — Arrays, dicts, tuples, records, choices, results:
//                      recursively deep-copy all contained values.
//   shared           — References: intentionally shared across copies.
//   function_isolate — Functions: deep-copy upvalue cells to isolate
//                      captured state (needed for task spawning).
//   collection       — CollectionObject subtypes: delegates to virtual
//                      deep_copy_value().

namespace luma {

// ─────────── Value::deep_copy ───────────

Value Value::deep_copy() const {
    const runtime::RecursionGuard guard{runtime::RecursionKind::deep_copy};
    if (!guard.entered()) {
        throw RuntimeError{"deep_copy: maximum nesting depth exceeded", {}};
    }

    switch (value_type()) {
        // Structural — recursively deep-copy elements/entries/fields.
        case ValueType::Array:
            return deep_copy_elements_collection(*as_array());

        case ValueType::Dictionary:
            return deep_copy_dictionary(*as_dictionary());

        case ValueType::Tuple:
            return deep_copy_elements_collection(*as_tuple());

        case ValueType::Record:
            return deep_copy_record(*as_record());

        case ValueType::Result:
            return deep_copy_result(*as_result());

        case ValueType::Choice:
            return deep_copy_choice(*as_choice());

        // Function isolate — deep-copy upvalue cells.
        case ValueType::Function:
            return deep_copy_function(*as_function());

        // Shared — reference cells are intentionally shared.
        case ValueType::Reference:
            return *this;

        // Collection — delegates to virtual deep_copy_value().
        case ValueType::Queue:
        case ValueType::Stack:
        case ValueType::Set:
        case ValueType::Xml:
        case ValueType::KeyValueStore:
        case ValueType::BinaryTree:
            return as_collection()->deep_copy_value();

        // Task handles are cloned so that each thread which receives a copy
        // (via spawn-argument deep-copy, channel send, or captured upvalue)
        // owns its OWN shared_future object over the same shared task state.
        // Two threads must never operate on the same shared_future object, but
        // distinct copies sharing the same state are safe.  The cancellation
        // token is shared so cancellation still propagates to the clone.
        case ValueType::Task: {
            const auto& source = as_task();
            return Value{std::make_shared<TaskValue>(source->future, source->token)};
        }

        // Shallow — primitives and opaque handles; a simple copy suffices.
        case ValueType::Null:
        case ValueType::Bool:
        case ValueType::Integer:
        case ValueType::Number:
        case ValueType::String:
        case ValueType::NativeFunction:
        case ValueType::Range:
        case ValueType::Channel:
        case ValueType::Socket:
        case ValueType::Decimal:
            return *this;
    }

    // Unreachable — all ValueType enumerators are handled above.
    // This exists only to silence compilers that warn on non-void
    // functions without a return after a fully-covered switch.
    return *this;
}

// ─────────── XmlValue::deep_clone ───────────

namespace {

// Depth-limited recursive clone.  Programmatically built XML trees are not
// bounded by the parser's incoming-nesting cap, so an unguarded recursive
// clone of a pathologically deep tree would overflow the native stack.  Mirror
// the parser's limit and raise a catchable RuntimeError instead of crashing.
std::shared_ptr<XmlValue> deep_clone_xml(const XmlValue& node, int depth) {
    if (depth > CompileTimeLimits::max_xml_depth) {
        throw RuntimeError{"XML nesting too deep", {}};
    }

    auto copy = std::make_shared<XmlValue>();
    copy->node_type = node.node_type;
    copy->tag_or_content = node.tag_or_content;
    copy->attributes = node.attributes;

    copy->children.reserve(node.children.size());

    for (const auto& child : node.children) {
        copy->children.push_back(deep_clone_xml(*child, depth + 1));
    }

    return copy;
}

} // namespace

std::shared_ptr<XmlValue> XmlValue::deep_clone() const {
    return deep_clone_xml(*this, 0);
}

// ─────────── CollectionObject::deep_copy_value implementations ───────────

// QueueValue, StackValue, SetValue — all vector-backed; delegate to shared helper.

Value QueueValue::deep_copy_value() const {
    return deep_copy_elements_collection(*this);
}

Value StackValue::deep_copy_value() const {
    return deep_copy_elements_collection(*this);
}

Value SetValue::deep_copy_value() const {
    return deep_copy_elements_collection(*this);
}

// XmlValue

Value XmlValue::deep_copy_value() const {
    return Value{deep_clone()};
}

// KeyValueStoreValue

Value KeyValueStoreValue::deep_copy_value() const {
    const std::scoped_lock lock{mutex};
    auto copy = std::make_shared<KeyValueStoreValue>();
    copy->entries = entries;
    copy->file_path = file_path;
    copy->read_only = read_only;

    return Value{std::move(copy)};
}

// BinaryTreeValue

static std::shared_ptr<BinaryTreeNode>
clone_binary_tree_node(const std::shared_ptr<BinaryTreeNode>& node) {
    if (!node) {
        return nullptr;
    }

    auto copy_node = std::make_shared<BinaryTreeNode>(node->value.deep_copy());
    copy_node->left = clone_binary_tree_node(node->left);
    copy_node->right = clone_binary_tree_node(node->right);

    return copy_node;
}

Value BinaryTreeValue::deep_copy_value() const {
    auto copy = std::make_shared<BinaryTreeValue>();
    copy->root = clone_binary_tree_node(root);
    copy->count_ = count_;

    return Value{std::move(copy)};
}

} // namespace luma
