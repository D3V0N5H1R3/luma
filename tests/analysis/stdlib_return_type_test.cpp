// Unit tests for shared/stdlib/stdlib_return_type.hpp.
//
// ReturnTypeDesc is the lightweight, dependency-free descriptor the shared
// stdlib catalog uses to record each function's return type; the type checker
// later converts it to a full TypeInfo.  These tests pin the factory helpers'
// kind, nesting, and named-type semantics so the catalog entries and the
// converter agree on the shape each factory produces.

#include <cstddef>
#include <string>

#include "stdlib/stdlib_return_type.hpp"
#include "test_framework.hpp"

using R = luma::stdlib::ReturnTypeDesc;

namespace {

// ASSERT_EQ streams its operands; the Kind enum is not streamable, so compare
// the underlying integers.
[[nodiscard]] int kind_of(const R& desc) {
    return static_cast<int>(desc.kind);
}

[[nodiscard]] int kind_value(R::Kind kind) {
    return static_cast<int>(kind);
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Defaults and primitive factories
// ═══════════════════════════════════════════════════════════

static void test_default_desc_is_unspecified() {
    const R desc{};
    ASSERT_EQ(kind_of(desc), kind_value(R::Unspecified));
    ASSERT_TRUE(desc.named_type.empty());
    ASSERT_TRUE(desc.inner.empty());
}

static void test_primitive_factories_have_expected_kinds() {
    ASSERT_EQ(kind_of(R::integer_type()), kind_value(R::Integer));
    ASSERT_EQ(kind_of(R::number_type()), kind_value(R::Number));
    ASSERT_EQ(kind_of(R::string_type()), kind_value(R::String));
    ASSERT_EQ(kind_of(R::boolean_type()), kind_value(R::Boolean));
    ASSERT_EQ(kind_of(R::void_type()), kind_value(R::Void));
    ASSERT_EQ(kind_of(R::none_type()), kind_value(R::None));
    ASSERT_EQ(kind_of(R::any_type()), kind_value(R::Any));
    ASSERT_EQ(kind_of(R::unspecified_type()), kind_value(R::Unspecified));
    ASSERT_EQ(kind_of(R::func_type()), kind_value(R::Func));
}

static void test_primitive_factories_carry_no_inner_or_name() {
    const R desc = R::integer_type();
    ASSERT_TRUE(desc.inner.empty());
    ASSERT_TRUE(desc.named_type.empty());
}

// ═══════════════════════════════════════════════════════════
// Generic factories nest their argument
// ═══════════════════════════════════════════════════════════

static void test_array_wraps_element_type() {
    const R desc = R::array(R::string_type());
    ASSERT_EQ(kind_of(desc), kind_value(R::Array));
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(kind_of(desc.inner[0]), kind_value(R::String));
}

static void test_dict_wraps_value_type() {
    const R desc = R::dict(R::number_type());
    ASSERT_EQ(kind_of(desc), kind_value(R::Dictionary));
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(kind_of(desc.inner[0]), kind_value(R::Number));
}

static void test_result_wraps_value_type() {
    const R desc = R::result(R::integer_type());
    ASSERT_EQ(kind_of(desc), kind_value(R::Result));
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(kind_of(desc.inner[0]), kind_value(R::Integer));
}

static void test_optional_channel_task_reference_wrap_value() {
    ASSERT_EQ(kind_of(R::optional(R::string_type())), kind_value(R::Optional));
    ASSERT_EQ(kind_of(R::channel(R::any_type())), kind_value(R::Channel));
    ASSERT_EQ(kind_of(R::task(R::any_type())), kind_value(R::Task));
    ASSERT_EQ(kind_of(R::reference(R::any_type())), kind_value(R::Reference));

    const R opt = R::optional(R::boolean_type());
    ASSERT_EQ(opt.inner.size(), 1U);
    ASSERT_EQ(kind_of(opt.inner[0]), kind_value(R::Boolean));
}

static void test_tuple_holds_all_elements_in_order() {
    const R desc = R::tuple({R::integer_type(), R::string_type(), R::boolean_type()});
    ASSERT_EQ(kind_of(desc), kind_value(R::Tuple));
    ASSERT_EQ(desc.inner.size(), 3U);
    ASSERT_EQ(kind_of(desc.inner[0]), kind_value(R::Integer));
    ASSERT_EQ(kind_of(desc.inner[1]), kind_value(R::String));
    ASSERT_EQ(kind_of(desc.inner[2]), kind_value(R::Boolean));
}

static void test_named_carries_type_name() {
    const R desc = R::named("TimeParts");
    ASSERT_EQ(kind_of(desc), kind_value(R::Named));
    ASSERT_EQ(desc.named_type, std::string{"TimeParts"});
    ASSERT_TRUE(desc.inner.empty());
}

// ═══════════════════════════════════════════════════════════
// Convenience shortcuts compose the generic factories
// ═══════════════════════════════════════════════════════════

static void test_result_scalar_shortcuts() {
    const R ri = R::result_integer();
    ASSERT_EQ(kind_of(ri), kind_value(R::Result));
    ASSERT_EQ(kind_of(ri.inner.at(0)), kind_value(R::Integer));

    ASSERT_EQ(kind_of(R::result_number().inner.at(0)), kind_value(R::Number));
    ASSERT_EQ(kind_of(R::result_string().inner.at(0)), kind_value(R::String));
    ASSERT_EQ(kind_of(R::result_boolean().inner.at(0)), kind_value(R::Boolean));
    ASSERT_EQ(kind_of(R::result_void().inner.at(0)), kind_value(R::Void));
    ASSERT_EQ(kind_of(R::result_any().inner.at(0)), kind_value(R::Any));
}

static void test_result_named_shortcut() {
    const R desc = R::result_named("Foo");
    ASSERT_EQ(kind_of(desc), kind_value(R::Result));
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(kind_of(desc.inner.at(0)), kind_value(R::Named));
    ASSERT_EQ(desc.inner.at(0).named_type, std::string{"Foo"});
}

static void test_result_array_any_is_doubly_nested() {
    const R desc = R::result_array_any();
    ASSERT_EQ(kind_of(desc), kind_value(R::Result));
    ASSERT_EQ(kind_of(desc.inner.at(0)), kind_value(R::Array));
    ASSERT_EQ(kind_of(desc.inner.at(0).inner.at(0)), kind_value(R::Any));
}

static void test_array_array_number_is_doubly_nested() {
    const R desc = R::array_array_number();
    ASSERT_EQ(kind_of(desc), kind_value(R::Array));
    ASSERT_EQ(kind_of(desc.inner.at(0)), kind_value(R::Array));
    ASSERT_EQ(kind_of(desc.inner.at(0).inner.at(0)), kind_value(R::Number));
}

static void test_array_and_dict_element_shortcuts() {
    ASSERT_EQ(kind_of(R::array_any().inner.at(0)), kind_value(R::Any));
    ASSERT_EQ(kind_of(R::array_string().inner.at(0)), kind_value(R::String));
    ASSERT_EQ(kind_of(R::array_integer().inner.at(0)), kind_value(R::Integer));
    ASSERT_EQ(kind_of(R::array_number().inner.at(0)), kind_value(R::Number));
    ASSERT_EQ(kind_of(R::dict_any().inner.at(0)), kind_value(R::Any));
    ASSERT_EQ(kind_of(R::dict_string().inner.at(0)), kind_value(R::String));
}

static void test_wrapper_any_shortcuts() {
    ASSERT_EQ(kind_of(R::optional_any()), kind_value(R::Optional));
    ASSERT_EQ(kind_of(R::optional_any().inner.at(0)), kind_value(R::Any));
    ASSERT_EQ(kind_of(R::channel_any()), kind_value(R::Channel));
    ASSERT_EQ(kind_of(R::task_any()), kind_value(R::Task));
    ASSERT_EQ(kind_of(R::reference_any()), kind_value(R::Reference));
}

// ─── main ───

int main() {
    using namespace luma::test;
    print_suite_header("stdlib_return_type");

    RUN(test_default_desc_is_unspecified);
    RUN(test_primitive_factories_have_expected_kinds);
    RUN(test_primitive_factories_carry_no_inner_or_name);

    RUN(test_array_wraps_element_type);
    RUN(test_dict_wraps_value_type);
    RUN(test_result_wraps_value_type);
    RUN(test_optional_channel_task_reference_wrap_value);
    RUN(test_tuple_holds_all_elements_in_order);
    RUN(test_named_carries_type_name);

    RUN(test_result_scalar_shortcuts);
    RUN(test_result_named_shortcut);
    RUN(test_result_array_any_is_doubly_nested);
    RUN(test_array_array_number_is_doubly_nested);
    RUN(test_array_and_dict_element_shortcuts);
    RUN(test_wrapper_any_shortcuts);

    return SUMMARY();
}
