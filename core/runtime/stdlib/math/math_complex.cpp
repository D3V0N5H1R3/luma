#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/math/math_module.hpp"

namespace luma {

namespace {

// A complex number as two doubles.  Real and imaginary parts are measurements,
// so both are Luma `number`.
struct Complex {
    double real;
    double imaginary;
};

// Build a Math.Complex record value.  The short runtime type_name "Complex"
// matches the "Math.Complex" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_complex(const Complex& c) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Complex";
    rec->fields.emplace_back("real", Value{c.real});
    rec->fields.emplace_back("imaginary", Value{c.imaginary});

    return Value{std::move(rec)};
}

// Read a Math.Complex argument.  Throws a RuntimeError when the value is not a
// complex-shaped record.  Accepting a hand-built record keeps every operation
// robust for values that did not come from Math.complex().
[[nodiscard]] Complex read_complex(const Value& value, std::string_view func,
                                   const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Complex record", loc,
                           "build one with Math.complex(real, imaginary)"};
    }

    const auto& rec = value.as_record();
    const Value* real_field = rec->find_field("real");
    const Value* imag_field = rec->find_field("imaginary");

    if (real_field == nullptr || !(real_field->is_integer() || real_field->is_number()) ||
        imag_field == nullptr || !(imag_field->is_integer() || imag_field->is_number())) {
        throw RuntimeError{std::string{func} + ": expected a Math.Complex record", loc,
                           "build one with Math.complex(real, imaginary)"};
    }

    return Complex{real_field->to_numeric(), imag_field->to_numeric()};
}

} // namespace

void register_math_complex(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        .func("complex", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto real = expect_numeric(args[0], "Math.complex", loc);
            const auto imaginary = expect_numeric(args[1], "Math.complex", loc);

            return make_complex(Complex{real, imaginary});
        })
        .func("complex_add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_complex(args[0], "Math.complex_add", loc);
            const auto b = read_complex(args[1], "Math.complex_add", loc);

            return make_complex(Complex{a.real + b.real, a.imaginary + b.imaginary});
        })
        .func("complex_subtract", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_complex(args[0], "Math.complex_subtract", loc);
            const auto b = read_complex(args[1], "Math.complex_subtract", loc);

            return make_complex(Complex{a.real - b.real, a.imaginary - b.imaginary});
        })
        .func("complex_multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_complex(args[0], "Math.complex_multiply", loc);
            const auto b = read_complex(args[1], "Math.complex_multiply", loc);

            // (a + bi)(c + di) = (ac - bd) + (ad + bc)i
            return make_complex(Complex{(a.real * b.real) - (a.imaginary * b.imaginary),
                                        (a.real * b.imaginary) + (a.imaginary * b.real)});
        })
        .func("complex_divide", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_complex(args[0], "Math.complex_divide", loc);
            const auto b = read_complex(args[1], "Math.complex_divide", loc);

            const double denom = (b.real * b.real) + (b.imaginary * b.imaginary);

            if (denom == 0.0) {
                return make_failure_value(
                    error_msg("Math", "complex_divide", "cannot divide by zero"));
            }

            // (a + bi) / (c + di) = ((ac + bd) + (bc - ad)i) / (c² + d²)
            const double re = ((a.real * b.real) + (a.imaginary * b.imaginary)) / denom;
            const double im = ((a.imaginary * b.real) - (a.real * b.imaginary)) / denom;

            return make_success_value(make_complex(Complex{re, im}));
        })
        .func("complex_magnitude", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_complex(args[0], "Math.complex_magnitude", loc);

            return Value{std::hypot(c.real, c.imaginary)};
        })
        .func("complex_conjugate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_complex(args[0], "Math.complex_conjugate", loc);

            return make_complex(Complex{c.real, -c.imaginary});
        })
        .func("complex_argument", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_complex(args[0], "Math.complex_argument", loc);

            return Value{std::atan2(c.imaginary, c.real)};
        });
}

} // namespace luma
