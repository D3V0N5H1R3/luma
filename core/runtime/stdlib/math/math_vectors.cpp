// Math module — typed 2D/3D geometry vectors and small transform matrices.
//
// Math.Vector2 { x, y }, Math.Vector3 { x, y, z }, Math.Matrix2 { m00..m11 } and
// Math.Matrix3 { m00..m22 } records plus minimal free-function families (vector
// add/sub/scale/dot/length/normalize/cross; matrix identity/multiply/determinant
// and vector-transform).  Named components make 2D/3D work far more teachable
// than the index arithmetic of LinearAlgebra's array<number>.  Data + pipe-first
// free functions, no operator overloading.  Registered via register_math_vectors().

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

// A 2×2 matrix in row-major order.  Every entry is a Luma `number`.
struct Mat2 {
    double m00, m01;
    double m10, m11;
};

// A 3×3 matrix in row-major order.
struct Mat3 {
    double m00, m01, m02;
    double m10, m11, m12;
    double m20, m21, m22;
};

// Build a Math.Matrix2 record value (type_name "Matrix2").
[[nodiscard]] Value make_mat2(const Mat2& m) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Matrix2";
    rec->fields.emplace_back("m00", Value{m.m00});
    rec->fields.emplace_back("m01", Value{m.m01});
    rec->fields.emplace_back("m10", Value{m.m10});
    rec->fields.emplace_back("m11", Value{m.m11});

    return Value{std::move(rec)};
}

// Build a Math.Matrix3 record value (type_name "Matrix3").
[[nodiscard]] Value make_mat3(const Mat3& m) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Matrix3";
    rec->fields.emplace_back("m00", Value{m.m00});
    rec->fields.emplace_back("m01", Value{m.m01});
    rec->fields.emplace_back("m02", Value{m.m02});
    rec->fields.emplace_back("m10", Value{m.m10});
    rec->fields.emplace_back("m11", Value{m.m11});
    rec->fields.emplace_back("m12", Value{m.m12});
    rec->fields.emplace_back("m20", Value{m.m20});
    rec->fields.emplace_back("m21", Value{m.m21});
    rec->fields.emplace_back("m22", Value{m.m22});

    return Value{std::move(rec)};
}

// Read a Math.Matrix2 argument.  Throws when the value is not a 2×2-matrix-shaped
// record; missing fields default to 0.0 so a hand-built record still works.
[[nodiscard]] Mat2 read_mat2(const Value& value, std::string_view func, const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Matrix2 record", loc,
                           "build one with Math.matrix2(m00, m01, m10, m11)"};
    }

    const auto& rec = value.as_record();
    const auto f = [&rec](std::string_view name) -> double {
        const Value* v = rec->find_field(name);
        return v != nullptr ? v->to_numeric() : 0.0;
    };

    return Mat2{f("m00"), f("m01"), f("m10"), f("m11")};
}

// Read a Math.Matrix3 argument.  Throws when the value is not a 3×3-matrix-shaped
// record; missing fields default to 0.0.
[[nodiscard]] Mat3 read_mat3(const Value& value, std::string_view func, const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Matrix3 record", loc,
                           "build one with Math.matrix3(m00, ..., m22)"};
    }

    const auto& rec = value.as_record();
    const auto f = [&rec](std::string_view name) -> double {
        const Value* v = rec->find_field(name);
        return v != nullptr ? v->to_numeric() : 0.0;
    };

    return Mat3{f("m00"), f("m01"), f("m02"), f("m10"), f("m11"),
                f("m12"), f("m20"), f("m21"), f("m22")};
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
        })
        // ── Math.Matrix2 ─────────────────────────────────────────────────────
        .func("matrix2", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return make_mat2(Mat2{expect_numeric(args[0], "Math.matrix2", loc),
                                  expect_numeric(args[1], "Math.matrix2", loc),
                                  expect_numeric(args[2], "Math.matrix2", loc),
                                  expect_numeric(args[3], "Math.matrix2", loc)});
        })
        .func("mat2_identity", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return make_mat2(Mat2{1.0, 0.0, 0.0, 1.0});
        })
        .func("mat2_multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_mat2(args[0], "Math.mat2_multiply", loc);
            const auto b = read_mat2(args[1], "Math.mat2_multiply", loc);

            return make_mat2(Mat2{(a.m00 * b.m00) + (a.m01 * b.m10),
                                  (a.m00 * b.m01) + (a.m01 * b.m11),
                                  (a.m10 * b.m00) + (a.m11 * b.m10),
                                  (a.m10 * b.m01) + (a.m11 * b.m11)});
        })
        .func("mat2_determinant", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto m = read_mat2(args[0], "Math.mat2_determinant", loc);

            return Value{(m.m00 * m.m11) - (m.m01 * m.m10)};
        })
        .func("mat2_transform", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto m = read_mat2(args[0], "Math.mat2_transform", loc);
            const auto v = read_vec2(args[1], "Math.mat2_transform", loc);

            return make_vec2(
                Vec2{(m.m00 * v.x) + (m.m01 * v.y), (m.m10 * v.x) + (m.m11 * v.y)});
        })
        // ── Math.Matrix3 ─────────────────────────────────────────────────────
        .func("matrix3", 9)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return make_mat3(Mat3{expect_numeric(args[0], "Math.matrix3", loc),
                                  expect_numeric(args[1], "Math.matrix3", loc),
                                  expect_numeric(args[2], "Math.matrix3", loc),
                                  expect_numeric(args[3], "Math.matrix3", loc),
                                  expect_numeric(args[4], "Math.matrix3", loc),
                                  expect_numeric(args[5], "Math.matrix3", loc),
                                  expect_numeric(args[6], "Math.matrix3", loc),
                                  expect_numeric(args[7], "Math.matrix3", loc),
                                  expect_numeric(args[8], "Math.matrix3", loc)});
        })
        .func("mat3_identity", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            return make_mat3(Mat3{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
        })
        .func("mat3_multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_mat3(args[0], "Math.mat3_multiply", loc);
            const auto b = read_mat3(args[1], "Math.mat3_multiply", loc);

            return make_mat3(Mat3{
                (a.m00 * b.m00) + (a.m01 * b.m10) + (a.m02 * b.m20),
                (a.m00 * b.m01) + (a.m01 * b.m11) + (a.m02 * b.m21),
                (a.m00 * b.m02) + (a.m01 * b.m12) + (a.m02 * b.m22),
                (a.m10 * b.m00) + (a.m11 * b.m10) + (a.m12 * b.m20),
                (a.m10 * b.m01) + (a.m11 * b.m11) + (a.m12 * b.m21),
                (a.m10 * b.m02) + (a.m11 * b.m12) + (a.m12 * b.m22),
                (a.m20 * b.m00) + (a.m21 * b.m10) + (a.m22 * b.m20),
                (a.m20 * b.m01) + (a.m21 * b.m11) + (a.m22 * b.m21),
                (a.m20 * b.m02) + (a.m21 * b.m12) + (a.m22 * b.m22)});
        })
        .func("mat3_determinant", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto m = read_mat3(args[0], "Math.mat3_determinant", loc);

            return Value{(m.m00 * ((m.m11 * m.m22) - (m.m12 * m.m21))) -
                         (m.m01 * ((m.m10 * m.m22) - (m.m12 * m.m20))) +
                         (m.m02 * ((m.m10 * m.m21) - (m.m11 * m.m20)))};
        })
        .func("mat3_transform", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto m = read_mat3(args[0], "Math.mat3_transform", loc);
            const auto v = read_vec3(args[1], "Math.mat3_transform", loc);

            return make_vec3(Vec3{(m.m00 * v.x) + (m.m01 * v.y) + (m.m02 * v.z),
                                  (m.m10 * v.x) + (m.m11 * v.y) + (m.m12 * v.z),
                                  (m.m20 * v.x) + (m.m21 * v.y) + (m.m22 * v.z)});
        });
}

} // namespace luma
