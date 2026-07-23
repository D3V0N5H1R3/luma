// json_module.cpp — JSON module registration.
//
// The serializer lives in json_module_serializer.cpp, the parser in
// json_module_parser.cpp, and the dot/bracket path navigation in
// json_module_path.{hpp,cpp}.  This translation unit registers every Json
// native function and holds their bodies.

#include "runtime/stdlib/text/json_module.hpp"

#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "common/index_validator.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"
#include "runtime/stdlib/text/json_module_path.hpp"

namespace luma {

// =============================================================================
// Registration
// =============================================================================

void register_json_ns(const EnvPtr& env) {
    ModuleBuilder{"Json", env}
        .func("serialize", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            std::string out;
            out.reserve(256);

            json_serialize_value(args[0], out, 0, 0, false);

            return Value{std::move(out)};
        })
        .func("serialize_pretty", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            std::string out;
            out.reserve(512);

            json_serialize_value(args[0], out, 2, 0, true);

            return Value{std::move(out)};
        });

    register_json_parser(env);
    register_json_value(env);
}

// =============================================================================
// Deserialization, path access, and mutation
// =============================================================================

void register_json_parser(const EnvPtr& env) {
    ModuleBuilder{"Json", env}
        .func("deserialize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.deserialize", loc);

            if (args[0].as_string().size() > ResourceLimits::max_string_size) {
                return make_failure_value(std::string{"Json.deserialize: input too large"});
            }

            return wrap_result_operation("Json", "deserialize", [&]() -> Value {
                return make_success_value(json_parse_string(args[0].as_string()));
            });
        })
        .func("is_valid", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.is_valid", loc);

            try {
                static_cast<void>(json_parse_string(args[0].as_string()));

                return Value{true};
            } catch (...) {
                return Value{false};
            }
        })
        .func("get", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.get", loc);

            (void)expect_string(args[1], "Json.get", loc);

            return wrap_result_operation("Json", "get", [&]() -> Value {
                auto root = json_parse_string(args[0].as_string());

                const auto segments = json_path::split_dot_path(args[1].as_string());

                if (segments.empty()) {
                    return make_success_value(std::move(root));
                }

                auto nav = json_path::navigate_path(root, segments, segments.size(),
                                                    json_path::ArrayKeyError::invalid_array_index);

                if (!nav.value) {
                    return make_failure_value(nav.error);
                }

                return make_success_value(Value{*nav.value});
            });
        })
        .func("set", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.set", loc);

            (void)expect_string(args[1], "Json.set", loc);

            return wrap_result_operation("Json", "set", [&]() -> Value {
                auto root = json_parse_string(args[0].as_string());

                const auto& path = args[1].as_string();
                const auto& new_val = args[2];

                const auto segments = json_path::split_dot_path(path);

                if (segments.empty()) {
                    return make_failure_value("Json.set: empty path");
                }

                // Navigate to the parent, rejecting arrays so Json.set stays a
                // dictionary-only walk (indexing arrays mid-path is Json.set_path's
                // job); set_path shares this same walker with arrays allowed.
                auto nav = json_path::navigate_path(root, segments, segments.size() - 1,
                                                    json_path::ArrayKeyError::cannot_use_key,
                                                    json_path::NavigateArrays::reject);

                if (!nav.value) {
                    return make_failure_value(nav.error);
                }

                if (nav.value->is_dictionary()) {
                    nav.value->as_dictionary()->set(segments.back().key, new_val);
                } else {
                    return make_failure_value("Json.set: target is not an object");
                }

                // Re-serialize.
                std::string out;
                out.reserve(256);

                json_serialize_value(root, out, 0, 0, false);

                return make_success_value(Value{std::move(out)});
            });
        })
        .func("merge", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.merge", loc);
            (void)expect_string(args[1], "Json.merge", loc);

            return wrap_result_operation("Json", "merge", [&]() -> Value {
                auto a = json_parse_string(args[0].as_string());
                auto b = json_parse_string(args[1].as_string());

                if (!a.is_dictionary() || !b.is_dictionary()) {
                    return make_failure_value("Json.merge: both inputs must be JSON objects");
                }

                auto merged = std::make_shared<DictionaryValue>();
                merged->entries = a.as_dictionary()->entries;
                merged->rebuild_index();

                for (const auto& [k, v] : b.as_dictionary()->entries) {
                    merged->set(k, v);
                }

                std::string out;
                out.reserve(256);

                json_serialize_value(Value{std::move(merged)}, out, 0, 0, false);

                return make_success_value(Value{std::move(out)});
            });
        })
        .func("get_path", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.get_path", loc);
            (void)expect_string(args[1], "Json.get_path", loc);

            return wrap_result_operation("Json", "get_path", [&]() -> Value {
                auto root = json_parse_string(args[0].as_string());

                const auto segments = json_path::parse_path_segments(args[1].as_string());

                if (segments.empty()) {
                    return make_success_value(std::move(root));
                }

                auto nav = json_path::navigate_path(root, segments, segments.size());

                if (!nav.value) {
                    return make_failure_value(error_msg("Json", "get_path", nav.error));
                }

                return make_success_value(Value{*nav.value});
            });
        })
        .func("set_path", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Json.set_path", loc);
            (void)expect_string(args[1], "Json.set_path", loc);

            return wrap_result_operation("Json", "set_path", [&]() -> Value {
                auto root = json_parse_string(args[0].as_string());

                const auto segments = json_path::parse_path_segments(args[1].as_string());

                if (segments.empty()) {
                    return make_failure_value("Json.set_path: empty path");
                }

                // Navigate to the parent of the target.
                auto nav = json_path::navigate_path(root, segments, segments.size() - 1);

                if (!nav.value) {
                    return make_failure_value(error_msg("Json", "set_path", nav.error));
                }

                const auto& last = segments.back();
                const Value* parent = nav.value;

                if (last.is_index) {
                    if (!parent->is_array()) {
                        return make_failure_value("Json.set_path: target parent is not an array");
                    }

                    auto& elems = *parent->as_array()->elements;

                    if (is_index_out_of_bounds(last.index, elems.size())) {
                        return make_failure_value(error_msg(
                            "Json", "set_path", std::format("index {} out of bounds", last.index)));
                    }

                    elems[static_cast<std::size_t>(last.index)] = args[2];
                } else if (parent->is_dictionary()) {
                    parent->as_dictionary()->set(last.key, args[2]);
                } else if (parent->is_array()) {
                    auto& elems = *parent->as_array()->elements;
                    auto resolved = json_path::resolve_array_index(
                        last.key, elems, json_path::ArrayKeyError::cannot_use_key);

                    if (!resolved.ok) {
                        return make_failure_value(error_msg("Json", "set_path", resolved.error));
                    }

                    elems[resolved.index] = args[2];
                } else {
                    return make_failure_value("Json.set_path: target is not a container");
                }

                std::string out;
                out.reserve(256);

                json_serialize_value(root, out, 0, 0, false);

                return make_success_value(Value{std::move(out)});
            });
        });
}

} // namespace luma
