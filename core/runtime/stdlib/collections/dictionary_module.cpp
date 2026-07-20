#include "runtime/stdlib/collections/dictionary_module.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <unordered_set>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"

namespace luma {

namespace {

// Ensures a Dictionary operation will not exceed ResourceLimits::max_dictionary_size.
// Call BEFORE the operation that grows the dictionary.  Centralises the size guard so
// every Dictionary site emits the same "Module.function: result exceeds maximum
// dictionary size" message and entry-count hint instead of drifting apart.
void ensure_dictionary_capacity(std::size_t current_size, std::size_t adding,
                                std::string_view function_name, const SourceLocation& loc) {
    // Overflow-safe comparison (mirrors validate_container_size): rearranged so that
    // current_size + adding cannot wrap past size_t max.
    if (adding > ResourceLimits::max_dictionary_size ||
        current_size > ResourceLimits::max_dictionary_size - adding) {
        throw RuntimeError{std::format("{}: result exceeds maximum dictionary size", function_name),
                           loc,
                           std::format("the maximum dictionary size is {} entries",
                                       ResourceLimits::max_dictionary_size)};
    }
}

// Encapsulates the recursive state (visited-set, source location) for
// deep-merging two dictionaries so that the recursive call signature
// stays small and the caller does not need to manage transient state.
class DictionaryMerger {
public:
    explicit DictionaryMerger(SourceLocation loc) : loc_{loc} {}

    [[nodiscard]] std::shared_ptr<DictionaryValue> merge(const DictionaryValue& base,
                                                         const DictionaryValue& overlay) {
        return merge_recursive(base, overlay, 0);
    }

private:
    // RAII guard that removes entries from the cycle-detection set on scope exit,
    // ensuring shared (non-cyclic) references are not falsely reported as cycles
    // when reached via a different branch.
    struct VisitedGuard {
        std::unordered_set<const DictionaryValue*>& set;
        const DictionaryValue* key;
        bool inserted_;

        VisitedGuard(std::unordered_set<const DictionaryValue*>& s, const DictionaryValue* k)
            : set{s}, key{k}, inserted_{s.insert(k).second} {}

        ~VisitedGuard() {
            set.erase(key);
        }

        [[nodiscard]] bool inserted() const noexcept {
            return inserted_;
        }
    };

    [[nodiscard]] std::shared_ptr<DictionaryValue>
    merge_recursive(const DictionaryValue& base, const DictionaryValue& overlay, int depth) {
        if (depth > CompileTimeLimits::max_merge_depth) {
            throw RuntimeError{"Dictionary.deep_merge: maximum nesting depth exceeded", loc_};
        }

        // Detect reference cycles.  Guards must be constructed before the
        // cycle check so that the inserted pointers are always cleaned up,
        // even when the check throws.  Base and overlay are tracked in
        // SEPARATE sets: a cycle exists only when the same node reappears on
        // its own side's recursion path.  Sharing one set would misreport
        // pointer identity across sides as a cycle — e.g. deep_merge(d, d), or
        // a base and overlay that share the same nested dictionary — even
        // though neither is cyclic.
        const VisitedGuard base_guard{base_visited_, &base};
        const VisitedGuard overlay_guard{overlay_visited_, &overlay};

        if (!base_guard.inserted() || !overlay_guard.inserted()) {
            throw RuntimeError{"Dictionary.deep_merge: circular reference detected", loc_};
        }

        auto result = std::make_shared<DictionaryValue>();
        result->entries = base.entries;
        result->rebuild_index();

        ensure_dictionary_capacity(result->entries.size(), overlay.entries.size(),
                                   "Dictionary.deep_merge", loc_);

        for (const auto& [k, v] : overlay.entries) {
            auto* existing = result->find(k);

            if (existing != nullptr) {
                if (existing->is_dictionary() && v.is_dictionary()) {
                    *existing = Value{
                        merge_recursive(*existing->as_dictionary(), *v.as_dictionary(), depth + 1)};
                } else {
                    *existing = v;
                }
            } else {
                result->set(k, v);
            }
        }

        return result;
    }

    SourceLocation loc_;
    std::unordered_set<const DictionaryValue*> base_visited_;
    std::unordered_set<const DictionaryValue*> overlay_visited_;
};

} // namespace

void register_dictionary_ns(const EnvPtr& env) {
    ModuleBuilder{"Dictionary", env}
        .func("has", 2)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args& args, SourceLocation loc) -> Value {
                          const auto& key = expect_string_key(args[1], "Dictionary.has", loc);
                          return Value{dict->find(key) != nullptr};
                      })
        .func("get", 2)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args& args, SourceLocation loc) -> Value {
                          const auto& key = expect_string_key(args[1], "Dictionary.get", loc);
                          const auto* val = dict->find(key);

                          if (val) {
                              return make_success_value(*val);
                          }
                          return make_failure_value(
                              ErrorMessages::key_not_found("Dictionary", "get", key));
                      })
        .func("get_or", 3)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args& args, SourceLocation loc) -> Value {
                          const auto& key = expect_string_key(args[1], "Dictionary.get_or", loc);
                          const auto* val = dict->find(key);
                          return val ? *val : args[2];
                      })
        .func("set", 3)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& key = expect_string_key(args[1], "Dictionary.set", loc);

                          if (!src->find(key)) {
                              ensure_dictionary_capacity(src->entries.size(), 1, "Dictionary.set",
                                                         loc);
                          }

                          auto dict = clone_dict(src);
                          dict->set(key, args[2]);
                          return Value{std::move(dict)};
                      })
        .func("remove", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& key = expect_string_key(args[1], "Dictionary.remove", loc);
                          auto dict = clone_dict(src);
                          dict->erase(key);
                          return Value{std::move(dict)};
                      })
        .func("length", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          return Value{static_cast<std::int64_t>(dict->entries.size())};
                      })
        .func("keys", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();
                          std::ranges::transform(
                              dict->entries, std::back_inserter(*arr->elements),
                              [](const auto& entry) { return Value{entry.first}; });
                          return Value{std::move(arr)};
                      })
        .func("values", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();
                          std::ranges::transform(dict->entries, std::back_inserter(*arr->elements),
                                                 [](const auto& entry) { return entry.second; });
                          return Value{std::move(arr)};
                      })
        .func("merge", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          auto dict = clone_dict(src);

                          const auto& other =
                              expect_dict(args[1], "Dictionary.merge", loc)->entries;

                          ensure_dictionary_capacity(dict->entries.size(), other.size(),
                                                     "Dictionary.merge", loc);

                          std::ranges::for_each(other, [&dict](const auto& entry) {
                              dict->set(entry.first, entry.second);
                          });
                          return Value{std::move(dict)};
                      })
        .func("is_empty", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          return Value{dict->entries.empty()};
                      })
        .func("each", 2)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args& args, SourceLocation loc) -> Value {
                          return dict_each(*dict, args[1], loc);
                      })
        .func("from_keys", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& keys = expect_array(args[0], "Dictionary.from_keys", loc);

            ensure_dictionary_capacity(0, keys->elements->size(), "Dictionary.from_keys", loc);
            expect_string_elements(*keys, "Dictionary.from_keys", loc);

            auto dict = std::make_shared<DictionaryValue>();
            // Build the (empty) hash index up front so each set() below takes the
            // O(1) hashed path instead of a linear scan, keeping the loop O(n).
            dict->rebuild_index();

            for (const auto& key : *keys->elements) {
                dict->set(key.as_string(), args[1]);
            }

            return Value{std::move(dict)};
        })
        .func("to_array", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          std::ranges::transform(
                              dict->entries, std::back_inserter(*arr->elements),
                              [](const auto& entry) {
                                  auto rec = std::make_shared<RecordValue>();
                                  rec->type_name = "KeyValue";
                                  rec->fields.emplace_back("key", Value{entry.first});
                                  rec->fields.emplace_back("value", entry.second);
                                  return Value{std::move(rec)};
                              });
                          return Value{std::move(arr)};
                      })
        // Dictionary.to_entries(dict) -> array<(string, V)>
        .func("to_entries", 1)
        .extract_body(expect_dict,
                      [](const auto& dict, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();

                          std::ranges::transform(dict->entries, std::back_inserter(*arr->elements),
                                                 [](const auto& entry) {
                                                     return make_tuple_pair(Value{entry.first},
                                                                            entry.second);
                                                 });
                          return Value{std::move(arr)};
                      })
        // Dictionary.from_entries(arr) -> dictionary<V>
        // Builds a dictionary from an array of (key, value) tuples.
        .func("from_entries", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& entries = expect_array(args[0], "Dictionary.from_entries", loc);

            ensure_dictionary_capacity(0, entries->elements->size(), "Dictionary.from_entries",
                                       loc);

            auto dict = std::make_shared<DictionaryValue>();
            // Build the (empty) hash index up front so each set() below takes the
            // O(1) hashed path instead of a linear scan, keeping the loop O(n).
            dict->rebuild_index();

            for (const auto& elem : *entries->elements) {
                if (!elem.is_tuple()) {
                    throw RuntimeError{"Dictionary.from_entries: each element must be a tuple", loc,
                                       "pass an array of (string, V) tuples"};
                }

                const auto& tuple = elem.as_tuple();

                if (tuple->elements.size() != 2) {
                    throw RuntimeError{"Dictionary.from_entries: each tuple must have exactly 2 "
                                       "elements",
                                       loc, "pass (key, value) tuples"};
                }

                if (!tuple->elements[0].is_string()) {
                    throw RuntimeError{"Dictionary.from_entries: tuple key must be a string", loc,
                                       "the first element of each tuple must be a string key"};
                }

                dict->set(tuple->elements[0].as_string(), tuple->elements[1]);
            }

            return Value{std::move(dict)};
        })
        .func("invert", 1)
        .extract_body(expect_dict,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto dict = std::make_shared<DictionaryValue>();
                          // Build the (empty) hash index up front so each set()
                          // below is O(1) rather than a linear scan.
                          dict->rebuild_index();
                          std::ranges::for_each(src->entries, [&dict](const auto& entry) {
                              dict->set(entry.second.to_string(), Value{entry.first});
                          });
                          return Value{std::move(dict)};
                      })
        .func("deep_merge", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return Value{DictionaryMerger{loc}.merge(
                              *src, *expect_dict(args[1], "Dictionary.deep_merge", loc))};
                      })
        .func("map_values", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return dict_map_values(*src, args[1], loc);
                      })
        .func("filter", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return dict_filter(*src, args[1], loc);
                      })
        .func("has_value", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          return Value{std::ranges::any_of(src->entries, [&](const auto& entry) {
                              return entry.second.equals(args[1]);
                          })};
                      })
        .func("reduce", 3)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return dict_reduce(*src, args[1], args[2], loc);
                      })
        .func("map", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return dict_map(*src, args[1], loc);
                      })
        .func("partition", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return dict_partition(*src, args[1], loc);
                      })
        .func("pick", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& keys = expect_array(args[1], "Dictionary.pick", loc);
                          expect_string_elements(*keys, "Dictionary.pick", loc);
                          auto dict = std::make_shared<DictionaryValue>();
                          // Build the (empty) hash index up front so each set()
                          // below is O(1) rather than a linear scan.
                          dict->rebuild_index();

                          for (const auto& key_val : *keys->elements) {
                              const auto& key = key_val.as_string();
                              if (const auto* val = src->find(key)) {
                                  dict->set(key, *val);
                              }
                          }
                          return Value{std::move(dict)};
                      })
        .func("omit", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& keys = expect_array(args[1], "Dictionary.omit", loc);
                          expect_string_elements(*keys, "Dictionary.omit", loc);
                          std::unordered_set<std::string> excluded;

                          for (const auto& key_val : *keys->elements) {
                              excluded.insert(key_val.as_string());
                          }

                          auto dict = std::make_shared<DictionaryValue>();
                          // Build the (empty) hash index up front so each set()
                          // below is O(1) rather than a linear scan.
                          dict->rebuild_index();
                          for (const auto& [k, v] : src->entries) {
                              if (!excluded.contains(k)) {
                                  dict->set(k, v);
                              }
                          }
                          return Value{std::move(dict)};
                      })
        .func("find", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Dictionary.find", loc);

                          return wrap_result_operation("Dictionary", "find", [&]() -> Value {
                              std::vector<Value> call_args(2);
                              for (const auto& [k, v] : src->entries) {
                                  call_args[0] = Value{k};
                                  call_args[1] = v;
                                  if (invoke_callable(args[1], call_args, loc).is_truthy()) {
                                      return make_success_value(make_tuple_pair(Value{k}, v));
                                  }
                              }
                              return make_failure_value("no entry matches the predicate");
                          });
                      })
        .func("count", 2)
        .extract_body(expect_dict,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Dictionary.count", loc);
                          return dict_count(*src, args[1], loc);
                      })
        .func("flip", 1)
        .extract_body(expect_dict, [](const auto& src, const Args&, SourceLocation) -> Value {
            auto dict = std::make_shared<DictionaryValue>();
            // Build the (empty) hash index up front so each set() below takes the
            // O(1) hashed path instead of a linear scan, keeping the loop O(n).
            dict->rebuild_index();

            for (const auto& [k, v] : src->entries) {
                if (!v.is_string()) {
                    return make_failure_value("Dictionary.flip: all values must be strings");
                }

                dict->set(v.as_string(), Value{k});
            }

            return make_success_value(Value{std::move(dict)});
        });
}

} // namespace luma
