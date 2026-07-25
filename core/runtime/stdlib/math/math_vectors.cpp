// Math module — typed 2D/3D geometry vectors.
//
// Math.Vector2 { x, y } and Math.Vector3 { x, y, z } records plus a minimal
// free-function family (add, sub, scale, dot, length, normalize, and — for 3D —
// cross).  Named .x/.y/.z components make 2D/3D work far more teachable than the
// index arithmetic of LinearAlgebra's array<number> vectors.  Data + pipe-first
// free functions, no operator overloading — the same philosophy as Math.Complex
// and its complex_* functions.  Registered via register_math_vectors().

#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/math/math_module.hpp"

namespace luma {

namespace {

// A 2D vector.  Components are measurements, so both are Luma `number`.
struct Vec2 {
    double x;
    double y;
};

// A 3D vector.  Components are measurements, so all three are Luma `number`.
struct Vec3 {
    double x;
    double y;
    double z;
};

// Build a Math.Vector2 record value.  The short runtime type_name "Vector2"
// matches the "Math.Vector2" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_vec2(const Vec2& v) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Vector2";
    rec->fields.emplace_back("x", Value{v.x});
    rec->fields.emplace_back("y", Value{v.y});

    return Value{std::move(rec)};
}

// Build a Math.Vector3 record value (type_name "Vector3").
[[nodiscard]] Value make_vec3(const Vec3& v) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Vector3";
    rec->fields.emplace_back("x", Value{v.x});
    rec->fields.emplace_back("y", Value{v.y});
    rec->fields.emplace_back("z", Value{v.z});

    return Value{std::move(rec)};
}

// Read a Math.Vector2 argument.  Throws when the value is not a 2D-vector-shaped
// record, so a hand-built record still works.
[[nodiscard]] Vec2 read_vec2(const Value& value, std::string_view func, const SourceLocation& loc) {
    const auto invalid = [&] {
        throw RuntimeError{std::string{func} + ": expected a Math.Vector2 record", loc,
                           "build one with Math.vector2(x, y)"};
    };

    if (!value.is_record()) {
        invalid();
    }

    const auto& rec = value.as_record();
    const Value* x = rec->find_field("x");
    const Value* y = rec->find_field("y");

    const auto numeric = [](const Value* v) {
        return v != nullptr && (v->is_integer() || v->is_number());
    };

    if (!numeric(x) || !numeric(y)) {
        invalid();
    }

    return Vec2{x->to_numeric(), y->to_numeric()};
}

// Read a Math.Vector3 argument.  Throws when the value is not a 3D-vector-shaped
// record.
[[nodiscard]] Vec3 read_vec3(const Value& value, std::string_view func, const SourceLocation& loc) {
    const auto invalid = [&] {
        throw RuntimeError{std::string{func} + ": expected a Math.Vector3 record", loc,
                           "build one with Math.vector3(x, y, z)"};
    };

    if (!value.is_record()) {
        invalid();
    }

    const auto& rec = value.as_record();
    const Value* x = rec->find_field("x");
    const Value* y = rec->find_field("y");
    const Value* z = rec->find_field("z");

    const auto numeric = [](const Value* v) {
        return v != nullptr && (v->is_integer() || v->is_number());
    };

    if (!numeric(x) || !numeric(y) || !numeric(z)) {
        invalid();
    }

    return Vec3{x->to_numeric(), y->to_numeric(), z->to_numeric()};
}

} // namespace

// Typed 2D/3D geometry vectors: constructors plus a minimal arithmetic family.
void register_math_vectors(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        // ── Math.Vector2 ─────────────────────────────────────────────────────
        .func("vector2", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.vector2", loc);
            const auto y = expect_numeric(args[1], "Math.vector2", loc);

            return make_vec2(Vec2{x, y});
        })
        .func("vec2_add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_add", loc);
            const auto b = read_vec2(args[1], "Math.vec2_add", loc);

            return make_vec2(Vec2{a.x + b.x, a.y + b.y});
        })
        .func("vec2_sub", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_sub", loc);
            const auto b = read_vec2(args[1], "Math.vec2_sub", loc);

            return make_vec2(Vec2{a.x - b.x, a.y - b.y});
        })
        .func("vec2_scale", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_scale", loc);
            const auto s = expect_numeric(args[1], "Math.vec2_scale", loc);

            return make_vec2(Vec2{a.x * s, a.y * s});
        })
        .func("vec2_dot", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_dot", loc);
            const auto b = read_vec2(args[1], "Math.vec2_dot", loc);

            return Value{(a.x * b.x) + (a.y * b.y)};
        })
        .func("vec2_length", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_length", loc);

            return Value{std::sqrt((a.x * a.x) + (a.y * a.y))};
        })
        .func("vec2_normalize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec2(args[0], "Math.vec2_normalize", loc);
            const double len = std::sqrt((a.x * a.x) + (a.y * a.y));

            // Normalising the zero vector is undefined; return it unchanged rather
            // than dividing by zero (mirrors LinearAlgebra.normalize's leniency).
            if (len == 0.0) {
                return make_vec2(a);
            }

            return make_vec2(Vec2{a.x / len, a.y / len});
        })
        // ── Math.Vector3 ─────────────────────────────────────────────────────
        .func("vector3", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.vector3", loc);
            const auto y = expect_numeric(args[1], "Math.vector3", loc);
            const auto z = expect_numeric(args[2], "Math.vector3", loc);

            return make_vec3(Vec3{x, y, z});
        })
        .func("vec3_add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_add", loc);
            const auto b = read_vec3(args[1], "Math.vec3_add", loc);

            return make_vec3(Vec3{a.x + b.x, a.y + b.y, a.z + b.z});
        })
        .func("vec3_sub", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_sub", loc);
            const auto b = read_vec3(args[1], "Math.vec3_sub", loc);

            return make_vec3(Vec3{a.x - b.x, a.y - b.y, a.z - b.z});
        })
        .func("vec3_scale", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_scale", loc);
            const auto s = expect_numeric(args[1], "Math.vec3_scale", loc);

            return make_vec3(Vec3{a.x * s, a.y * s, a.z * s});
        })
        .func("vec3_dot", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_dot", loc);
            const auto b = read_vec3(args[1], "Math.vec3_dot", loc);

            return Value{(a.x * b.x) + (a.y * b.y) + (a.z * b.z)};
        })
        .func("vec3_cross", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_cross", loc);
            const auto b = read_vec3(args[1], "Math.vec3_cross", loc);

            return make_vec3(Vec3{(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z),
                                  (a.x * b.y) - (a.y * b.x)});
        })
        .func("vec3_length", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_length", loc);

            return Value{std::sqrt((a.x * a.x) + (a.y * a.y) + (a.z * a.z))};
        })
        .func("vec3_normalize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_vec3(args[0], "Math.vec3_normalize", loc);
            const double len = std::sqrt((a.x * a.x) + (a.y * a.y) + (a.z * a.z));

            if (len == 0.0) {
                return make_vec3(a);
            }

            return make_vec3(Vec3{a.x / len, a.y / len, a.z / len});
        });
}

} // namespace luma
