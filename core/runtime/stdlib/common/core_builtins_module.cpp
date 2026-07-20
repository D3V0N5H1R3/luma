#include "runtime/stdlib/common/core_builtins_module.hpp"

#include <iostream>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

// ─── ModuleBuilder note ───────────────────────────────────────────────────────
// This file intentionally uses `define_native` directly rather than
// `ModuleBuilder`.  `print`, `assert`, and `type_of` are unqualified
// language-level built-ins with no module prefix — they are resolved as plain
// identifiers in the global environment.  `ModuleBuilder` always qualifies
// every name as "<module>.<func>", so it cannot be used for global functions.
// ─────────────────────────────────────────────────────────────────────────────

void register_core(const EnvPtr& env) {
    define_native(env, "print", [](std::span<const Value> args, SourceLocation) -> Value {
        bool first{true};

        for (const auto& arg : args) {
            if (!first) {
                std::cout << ' ';
            }

            first = false;

            std::cout << arg.to_string();
        }

        std::cout << "\n";

        return NullValue{};
    });

    define_native(env, "assert", [](std::span<const Value> args, SourceLocation loc) -> Value {
        expect_min_args("assert", args, 1, loc);

        if (args.size() >= 2) {
            validate_type(
                args[1], [](const Value& val) { return val.is_string(); }, "string", "assert", loc,
                "pass a string literal as the assertion message");
        }

        if (!args[0].is_truthy()) {
            const std::string msg{args.size() >= 2 ? args[1].as_string() : "assertion failed"};

            throw RuntimeError{msg, loc};
        }

        return NullValue{};
    });

    define_native(env, "type_of", [](std::span<const Value> args, SourceLocation loc) -> Value {
        expect_args("type_of", args, 1, loc);

        return Value{args[0].display_type_name()};
    });
}

} // namespace luma
