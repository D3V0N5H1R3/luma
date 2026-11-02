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

// ═══════════════════════════════════════════════════════════
// Defaults and primitive factories
// ═══════════════════════════════════════════════════════════

static void test_default_desc_is_unspecified() {
    const R desc{};
    ASSERT_EQ(desc.kind, R::Kind::Unspecified);
    ASSERT_TRUE(desc.named_type.empty());
    ASSERT_TRUE(desc.inner.empty());
}

static void test_primitive_factories_have_expected_kinds() {
    ASSERT_EQ(R::integer_type().kind, R::Kind::Integer);
    ASSERT_EQ(R::number_type().kind, R::Kind::Number);
    ASSERT_EQ(R::string_type().kind, R::Kind::String);
    ASSERT_EQ(R::boolean_type().kind, R::Kind::Boolean);
    ASSERT_EQ(R::void_type().kind, R::Kind::Void);
    ASSERT_EQ(R::none_type().kind, R::Kind::None);
    ASSERT_EQ(R::any_type().kind, R::Kind::Any);
    ASSERT_EQ(R::unspecified_type().kind, R::Kind::Unspecified);
    ASSERT_EQ(R::func_type().kind, R::Kind::Func);
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
    ASSERT_EQ(desc.kind, R::Kind::Array);
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(desc.inner[0].kind, R::Kind::String);
}

static void test_dict_wraps_value_type() {
    const R desc = R::dict(R::number_type());
    ASSERT_EQ(desc.kind, R::Kind::Dictionary);
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(desc.inner[0].kind, R::Kind::Number);
}

static void test_result_wraps_value_type() {
    const R desc = R::result(R::integer_type());
    ASSERT_EQ(desc.kind, R::Kind::Result);
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(desc.inner[0].kind, R::Kind::Integer);
}

static void test_optional_channel_task_reference_wrap_value() {
    ASSERT_EQ(R::optional(R::string_type()).kind, R::Kind::Optional);
    ASSERT_EQ(R::channel(R::any_type()).kind, R::Kind::Channel);
    ASSERT_EQ(R::task(R::any_type()).kind, R::Kind::Task);
    ASSERT_EQ(R::reference(R::any_type()).kind, R::Kind::Reference);

    const R opt = R::optional(R::boolean_type());
    ASSERT_EQ(opt.inner.size(), 1U);
    ASSERT_EQ(opt.inner[0].kind, R::Kind::Boolean);
}

static void test_tuple_holds_all_elements_in_order() {
    const R desc = R::tuple({R::integer_type(), R::string_type(), R::boolean_type()});
    ASSERT_EQ(desc.kind, R::Kind::Tuple);
    ASSERT_EQ(desc.inner.size(), 3U);
    ASSERT_EQ(desc.inner[0].kind, R::Kind::Integer);
    ASSERT_EQ(desc.inner[1].kind, R::Kind::String);
    ASSERT_EQ(desc.inner[2].kind, R::Kind::Boolean);
}

static void test_named_carries_type_name() {
    const R desc = R::named("TimeParts");
    ASSERT_EQ(desc.kind, R::Kind::Named);
    ASSERT_EQ(desc.named_type, std::string{"TimeParts"});
    ASSERT_TRUE(desc.inner.empty());
}

// ═══════════════════════════════════════════════════════════
// Convenience shortcuts compose the generic factories
// ═══════════════════════════════════════════════════════════

static void test_result_scalar_shortcuts() {
    const R ri = R::result_integer();
    ASSERT_EQ(ri.kind, R::Kind::Result);
    ASSERT_EQ(ri.inner.at(0).kind, R::Kind::Integer);

    ASSERT_EQ(R::result_number().inner.at(0).kind, R::Kind::Number);
    ASSERT_EQ(R::result_string().inner.at(0).kind, R::Kind::String);
    ASSERT_EQ(R::result_boolean().inner.at(0).kind, R::Kind::Boolean);
    ASSERT_EQ(R::result_void().inner.at(0).kind, R::Kind::Void);
    ASSERT_EQ(R::result_any().inner.at(0).kind, R::Kind::Any);
}

static void test_result_named_shortcut() {
    const R desc = R::result_named("Foo");
    ASSERT_EQ(desc.kind, R::Kind::Result);
    ASSERT_EQ(desc.inner.size(), 1U);
    ASSERT_EQ(desc.inner.at(0).kind, R::Kind::Named);
    ASSERT_EQ(desc.inner.at(0).named_type, std::string{"Foo"});
}

static void test_result_array_any_is_doubly_nested() {
    const R desc = R::result_array_any();
    ASSERT_EQ(desc.kind, R::Kind::Result);
    ASSERT_EQ(desc.inner.at(0).kind, R::Kind::Array);
    ASSERT_EQ(desc.inner.at(0).inner.at(0).kind, R::Kind::Any);
}

static void test_array_array_number_is_doubly_nested() {
    const R desc = R::array_array_number();
    ASSERT_EQ(desc.kind, R::Kind::Array);
    ASSERT_EQ(desc.inner.at(0).kind, R::Kind::Array);
    ASSERT_EQ(desc.inner.at(0).inner.at(0).kind, R::Kind::Number);
}

static void test_array_and_dict_element_shortcuts() {
    ASSERT_EQ(R::array_any().inner.at(0).kind, R::Kind::Any);
    ASSERT_EQ(R::array_string().inner.at(0).kind, R::Kind::String);
    ASSERT_EQ(R::array_integer().inner.at(0).kind, R::Kind::Integer);
    ASSERT_EQ(R::array_number().inner.at(0).kind, R::Kind::Number);
    ASSERT_EQ(R::dict_any().inner.at(0).kind, R::Kind::Any);
    ASSERT_EQ(R::dict_string().inner.at(0).kind, R::Kind::String);
}

static void test_wrapper_any_shortcuts() {
    ASSERT_EQ(R::optional_any().kind, R::Kind::Optional);
    ASSERT_EQ(R::optional_any().inner.at(0).kind, R::Kind::Any);
    ASSERT_EQ(R::channel_any().kind, R::Kind::Channel);
    ASSERT_EQ(R::task_any().kind, R::Kind::Task);
    ASSERT_EQ(R::reference_any().kind, R::Kind::Reference);
}

// ─── main ───

int main() {
    luma::test::print_suite_header("stdlib_return_type");

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
