// Standard library tests: Order (Ordering choice + comparison helpers).

#include <string>

#include "stdlib_test_helpers.hpp"

// ─── Order.of ───────────────────────────────────────────────────────

static void test_order_of_integers() {
    const auto less = eval("Order.of(1, 2)");
    ASSERT_TRUE(less.is_choice());
    ASSERT_EQ(less.as_choice()->type_name, "Ordering");
    ASSERT_EQ(less.as_choice()->variant, "Less");

    ASSERT_EQ(eval("Order.of(2, 2)").as_choice()->variant, "Equal");
    ASSERT_EQ(eval("Order.of(3, 2)").as_choice()->variant, "Greater");
}

static void test_order_of_numbers() {
    ASSERT_EQ(eval("Order.of(1.5, 2.5)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.of(2.5, 2.5)").as_choice()->variant, "Equal");
    ASSERT_EQ(eval("Order.of(9.0, 2.5)").as_choice()->variant, "Greater");
}

static void test_order_of_mixed_integer_number() {
    // integer widens to number for comparison.
    ASSERT_EQ(eval("Order.of(1, 2.5)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.of(3, 2.5)").as_choice()->variant, "Greater");
}

static void test_order_of_strings() {
    ASSERT_EQ(eval(R"(Order.of("apple", "banana"))").as_choice()->variant, "Less");
    ASSERT_EQ(eval(R"(Order.of("pear", "pear"))").as_choice()->variant, "Equal");
    ASSERT_EQ(eval(R"(Order.of("cherry", "banana"))").as_choice()->variant, "Greater");
}

static void test_order_of_booleans() {
    // false < true.
    ASSERT_EQ(eval("Order.of(false, true)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.of(true, true)").as_choice()->variant, "Equal");
    ASSERT_EQ(eval("Order.of(true, false)").as_choice()->variant, "Greater");
}

static void test_order_of_rejects_incomparable() {
    // Mismatched, non-promotable types are a programmer error.
    ASSERT_TRUE(luma::test::eval_throws(R"(Order.of(1, "a"))"));
    ASSERT_TRUE(luma::test::eval_throws("Order.of(true, 1)"));
}

// ─── Order.reverse ──────────────────────────────────────────────────

static void test_order_reverse() {
    ASSERT_EQ(eval("Order.reverse(Ordering.Less)").as_choice()->variant, "Greater");
    ASSERT_EQ(eval("Order.reverse(Ordering.Equal)").as_choice()->variant, "Equal");
    ASSERT_EQ(eval("Order.reverse(Ordering.Greater)").as_choice()->variant, "Less");
}

static void test_order_reverse_type_name() {
    ASSERT_EQ(eval("Order.reverse(Ordering.Less)").as_choice()->type_name, "Ordering");
}

// ─── Order.then ─────────────────────────────────────────────────────

static void test_order_then_first_decides() {
    // A non-Equal first result wins the tie-break outright.
    ASSERT_EQ(eval("Order.then(Ordering.Less, Ordering.Greater)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.then(Ordering.Greater, Ordering.Less)").as_choice()->variant, "Greater");
}

static void test_order_then_falls_back_on_equal() {
    // An Equal first result defers to the second key.
    ASSERT_EQ(eval("Order.then(Ordering.Equal, Ordering.Less)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.then(Ordering.Equal, Ordering.Equal)").as_choice()->variant, "Equal");
}

// ─── Order.to_number / from_number ──────────────────────────────────

static void test_order_to_number() {
    ASSERT_EQ(eval("Order.to_number(Ordering.Less)").as_number(), -1.0);
    ASSERT_EQ(eval("Order.to_number(Ordering.Equal)").as_number(), 0.0);
    ASSERT_EQ(eval("Order.to_number(Ordering.Greater)").as_number(), 1.0);
}

static void test_order_from_number() {
    ASSERT_EQ(eval("Order.from_number(-5.0)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.from_number(0.0)").as_choice()->variant, "Equal");
    ASSERT_EQ(eval("Order.from_number(42.0)").as_choice()->variant, "Greater");
}

static void test_order_from_number_accepts_integer() {
    // integer widens to number, so from_number accepts a raw comparator sign.
    ASSERT_EQ(eval("Order.from_number(-1)").as_choice()->variant, "Less");
    ASSERT_EQ(eval("Order.from_number(1)").as_choice()->variant, "Greater");
}

static void test_order_number_roundtrip() {
    // from_number ∘ to_number is the identity on the three variants.
    for (const std::string variant : {"Less", "Equal", "Greater"}) {
        const auto expr = "Order.from_number(Order.to_number(Ordering." + variant + "))";
        ASSERT_EQ(eval(expr).as_choice()->variant, variant);
    }
}

// ─── Type safety ────────────────────────────────────────────────────

static void test_order_accessors_reject_non_ordering() {
    // The Ordering-consuming helpers reject a non-choice and a foreign choice.
    ASSERT_TRUE(luma::test::eval_throws("Order.reverse(5)"));
    ASSERT_TRUE(luma::test::eval_throws("Order.to_number(Log.Level.Information)"));
    ASSERT_TRUE(luma::test::eval_throws("Order.then(Ordering.Less, 0)"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Order.from_number("x"))"));
}

int main() {
    RUN(test_order_of_integers);
    RUN(test_order_of_numbers);
    RUN(test_order_of_mixed_integer_number);
    RUN(test_order_of_strings);
    RUN(test_order_of_booleans);
    RUN(test_order_of_rejects_incomparable);
    RUN(test_order_reverse);
    RUN(test_order_reverse_type_name);
    RUN(test_order_then_first_decides);
    RUN(test_order_then_falls_back_on_equal);
    RUN(test_order_to_number);
    RUN(test_order_from_number);
    RUN(test_order_from_number_accepts_integer);
    RUN(test_order_number_roundtrip);
    RUN(test_order_accessors_reject_non_ordering);
    return SUMMARY();
}
