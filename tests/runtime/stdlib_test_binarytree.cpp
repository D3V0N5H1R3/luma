// Standard library tests: BinaryTree.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_binarytree_filter() {
    const auto v = eval("BinaryTree.filter("
                        "BinaryTree.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 2)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tree = v.as_result()->owned_inner->as_binary_tree();

    ASSERT_EQ(tree->count_, 3);
}

static void test_binarytree_filter_empty() {
    const auto v = eval("BinaryTree.filter("
                        "BinaryTree.from_array([1, 2, 3]),"
                        "(integer x) -> x > 10)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tree = v.as_result()->owned_inner->as_binary_tree();

    ASSERT_EQ(tree->count_, 0);
}

static void test_binarytree_floor_ceiling() {
    // floor — largest value ≤ key
    ASSERT_EVAL_INT("BinaryTree.from_array([1, 3, 5, 7]) |> BinaryTree.floor_value(4)", 3);

    // ceiling — smallest value ≥ key
    ASSERT_EVAL_INT("BinaryTree.from_array([1, 3, 5, 7]) |> BinaryTree.ceiling_value(4)", 5);

    // exact match is its own floor/ceiling
    ASSERT_EVAL_INT("BinaryTree.from_array([1, 3, 5]) |> BinaryTree.floor_value(3)", 3);

    // no floor — key smaller than minimum
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([5, 7]) |> BinaryTree.floor_value(2)");
}

static void test_binarytree_ceiling_no_ceiling() {
    // no ceiling — key larger than maximum
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([1, 3]) |> BinaryTree.ceiling_value(10)");
}

static void test_binarytree_from_array() {
    ASSERT_EQ(eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.length()").as_integer(),
              5);
}

static void test_binarytree_height() {
    ASSERT_TRUE(eval("BinaryTree.from_array([5, 3, 7]) |> BinaryTree.height()").as_integer() >= 2);
}

static void test_binarytree_inorder() {
    const auto result = eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.inorder()");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{5});
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*result.as_array()->elements)[4].as_integer(), 9);
}

static void test_binarytree_preorder() {
    // Tree shape for [5, 3, 7, 1, 9]: 5 root, 3/7 children, 1 under 3, 9 under 7.
    const auto result = eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.preorder()");

    ASSERT_TRUE(result.is_array());

    const auto& e = *result.as_array()->elements;

    ASSERT_EQ(e.size(), std::size_t{5});
    ASSERT_EQ(e[0].as_integer(), 5);
    ASSERT_EQ(e[1].as_integer(), 3);
    ASSERT_EQ(e[2].as_integer(), 1);
    ASSERT_EQ(e[3].as_integer(), 7);
    ASSERT_EQ(e[4].as_integer(), 9);
}

static void test_binarytree_postorder() {
    const auto result = eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.postorder()");

    ASSERT_TRUE(result.is_array());

    const auto& e = *result.as_array()->elements;

    ASSERT_EQ(e.size(), std::size_t{5});
    ASSERT_EQ(e[0].as_integer(), 1);
    ASSERT_EQ(e[1].as_integer(), 3);
    ASSERT_EQ(e[2].as_integer(), 9);
    ASSERT_EQ(e[3].as_integer(), 7);
    ASSERT_EQ(e[4].as_integer(), 5);
}

static void test_binarytree_level_order() {
    const auto result = eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.level_order()");

    ASSERT_TRUE(result.is_array());

    const auto& e = *result.as_array()->elements;

    ASSERT_EQ(e.size(), std::size_t{5});
    ASSERT_EQ(e[0].as_integer(), 5);
    ASSERT_EQ(e[1].as_integer(), 3);
    ASSERT_EQ(e[2].as_integer(), 7);
    ASSERT_EQ(e[3].as_integer(), 1);
    ASSERT_EQ(e[4].as_integer(), 9);
}

static void test_binarytree_insert_contains() {
    ASSERT_TRUE(
        eval("BinaryTree.new() |> BinaryTree.insert(5) |> BinaryTree.contains(5)").as_bool());
    ASSERT_TRUE(
        !eval("BinaryTree.new() |> BinaryTree.insert(5) |> BinaryTree.contains(3)").as_bool());
}

static void test_binarytree_is_empty() {
    ASSERT_TRUE(eval("BinaryTree.new() |> BinaryTree.is_empty()").as_bool());
    ASSERT_FALSE(eval("BinaryTree.from_array([1]) |> BinaryTree.is_empty()").as_bool());
}

static void test_binarytree_min_max() {
    ASSERT_EQ(eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.min() |> Result.unwrap()")
                  .as_integer(),
              1);
    ASSERT_EQ(eval("BinaryTree.from_array([5, 3, 7, 1, 9]) |> BinaryTree.max() |> Result.unwrap()")
                  .as_integer(),
              9);
}

static void test_binarytree_min_max_empty() {
    ASSERT_EVAL_FAILURE("BinaryTree.new() |> BinaryTree.min()");

    ASSERT_EVAL_FAILURE("BinaryTree.new() |> BinaryTree.max()");
}

static void test_binarytree_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("BinaryTree.new"));
    ASSERT_TRUE(env->has("BinaryTree.from_array"));
    ASSERT_TRUE(env->has("BinaryTree.insert"));
    ASSERT_TRUE(env->has("BinaryTree.remove"));
    ASSERT_TRUE(env->has("BinaryTree.contains"));
    ASSERT_TRUE(env->has("BinaryTree.length"));
    ASSERT_TRUE(env->has("BinaryTree.is_empty"));
    ASSERT_TRUE(env->has("BinaryTree.height"));
    ASSERT_TRUE(env->has("BinaryTree.min"));
    ASSERT_TRUE(env->has("BinaryTree.max"));
    ASSERT_TRUE(env->has("BinaryTree.inorder"));
    ASSERT_TRUE(env->has("BinaryTree.preorder"));
    ASSERT_TRUE(env->has("BinaryTree.postorder"));
    ASSERT_TRUE(env->has("BinaryTree.level_order"));
    ASSERT_TRUE(env->has("BinaryTree.to_array"));
    ASSERT_TRUE(env->has("BinaryTree.floor_value"));
    ASSERT_TRUE(env->has("BinaryTree.ceiling_value"));
}

static void test_binarytree_partition() {
    const auto v = eval("BinaryTree.partition("
                        "BinaryTree.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_TRUE(tup[0].is_binary_tree());
    ASSERT_TRUE(tup[1].is_binary_tree());
    ASSERT_EQ(tup[0].as_binary_tree()->count_, 2);
    ASSERT_EQ(tup[1].as_binary_tree()->count_, 3);
}

static void test_binarytree_reduce() {
    ASSERT_EVAL_INT("BinaryTree.reduce("
                    "BinaryTree.from_array([1, 2, 3, 4, 5]),"
                    "0,"
                    "(integer acc, integer x) -> acc + x)",
                    15);
}

static void test_binarytree_reduce_empty() {
    ASSERT_EVAL_INT("BinaryTree.reduce("
                    "BinaryTree.new(),"
                    "42,"
                    "(integer acc, integer x) -> acc + x)",
                    42);
}

static void test_binarytree_remove() {
    ASSERT_EQ(
        eval("BinaryTree.from_array([5, 3, 7]) |> BinaryTree.remove(3) |> BinaryTree.length()")
            .as_integer(),
        2);
}

static void test_binarytree_to_array() {
    const auto result = eval("BinaryTree.from_array([3, 1, 2]) |> BinaryTree.to_array()");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*result.as_array()->elements)[1].as_integer(), 2);
    ASSERT_EQ((*result.as_array()->elements)[2].as_integer(), 3);
}

static void test_binarytree_union() {
    const auto v = eval("BinaryTree.to_array(BinaryTree.union("
                        "BinaryTree.from_array([1, 3, 5]),"
                        "BinaryTree.from_array([2, 3, 4])))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 5U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[4].as_integer(), 5);
}

static void test_binarytree_balance() {
    // Build an unbalanced tree and balance it — size should be preserved.
    const auto v = eval("BinaryTree.new()"
                        "|> BinaryTree.insert(1)"
                        "|> BinaryTree.insert(2)"
                        "|> BinaryTree.insert(3)"
                        "|> BinaryTree.insert(4)"
                        "|> BinaryTree.insert(5)"
                        "|> BinaryTree.balance()");

    ASSERT_TRUE(v.is_binary_tree());
    ASSERT_EQ(v.as_binary_tree()->count_, 5);
}

static void test_binarytree_balance_preserves_elements() {
    // After balancing, all original elements should still be findable.
    const auto v = eval("binary_tree t = BinaryTree.from_array([3, 1, 4, 1, 5, 9, 2, 6])\n"
                        "|> BinaryTree.balance()\n"
                        "BinaryTree.contains(t, 9)");

    ASSERT_TRUE(v.as_bool());
}

static void test_binarytree_balance_empty() {
    const auto v = eval("BinaryTree.new() |> BinaryTree.balance()");

    ASSERT_TRUE(v.is_binary_tree());
    ASSERT_EQ(v.as_binary_tree()->count_, 0);
}

static void test_binarytree_successor() {
    ASSERT_EVAL_INT("BinaryTree.from_array([1, 3, 5, 7, 9])"
                    "|> BinaryTree.successor(3)",
                    5);
}

static void test_binarytree_successor_last() {
    // Successor of the largest element should fail.
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([1, 3, 5])"
                        "|> BinaryTree.successor(5)");
}

static void test_binarytree_successor_not_found() {
    // Successor of a value larger than all elements should fail.
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([1, 3, 5])"
                        "|> BinaryTree.successor(6)");
}

static void test_binarytree_predecessor() {
    ASSERT_EVAL_INT("BinaryTree.from_array([1, 3, 5, 7, 9])"
                    "|> BinaryTree.predecessor(7)",
                    5);
}

static void test_binarytree_predecessor_first() {
    // Predecessor of the smallest element should fail.
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([1, 3, 5])"
                        "|> BinaryTree.predecessor(1)");
}

static void test_binarytree_predecessor_not_found() {
    // Predecessor of a value smaller than every element should fail.
    ASSERT_EVAL_FAILURE("BinaryTree.from_array([1, 3, 5])"
                        "|> BinaryTree.predecessor(0)");
}

// ─── Incomparable-element error paths ──────────────────────────────────
// A binary_tree stores values as `any`, so the type checker permits trees of
// arrays (or otherwise incomparable values). Ordering them fails at runtime
// when the BST comparison is reached; every comparing operation must surface
// that failure as a thrown error rather than silently misbehaving.

static void test_binarytree_from_array_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.from_array([[1], [2]])"));
}

static void test_binarytree_insert_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.insert(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_contains_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.contains(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_remove_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.remove(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_floor_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.floor_value(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_ceiling_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.ceiling_value(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_successor_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.successor(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_predecessor_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.predecessor(BinaryTree.from_array([1, 2, 3]), [5])"));
}

static void test_binarytree_union_incomparable_throws() {
    ASSERT_THROWS(eval("BinaryTree.union("
                       "BinaryTree.from_array([1, 2, 3]),"
                       "BinaryTree.from_array([[5]]))"));
}

static void test_binarytree_insert_caps_size() {
    // Regression: BinaryTree.insert grew count_ with no size cap, while every
    // other collection (Array, Dictionary, Queue, Stack, Set) caps
    // at its max_*_size. The depth guard in insert_node only bounds a
    // degenerate tree — a balanced tree can hold ~2^max_tree_depth nodes — so a
    // loop of inserts could grow the tree until memory is exhausted. Lower the
    // cap so a handful of inserts trips it.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    // Filling exactly to the cap succeeds.
    ASSERT_EQ(eval("BinaryTree.new() |> BinaryTree.insert(1) |> BinaryTree.insert(2) "
                   "|> BinaryTree.insert(3) |> BinaryTree.length()")
                  .as_integer(),
              3);

    // The fourth distinct insert exceeds the cap and throws.
    ASSERT_THROWS(eval("BinaryTree.new() |> BinaryTree.insert(1) |> BinaryTree.insert(2) "
                       "|> BinaryTree.insert(3) |> BinaryTree.insert(4)"));

    // A duplicate insert at the cap doesn't grow the tree, so it still succeeds.
    ASSERT_EQ(eval("BinaryTree.new() |> BinaryTree.insert(1) |> BinaryTree.insert(2) "
                   "|> BinaryTree.insert(3) |> BinaryTree.insert(2) |> BinaryTree.length()")
                  .as_integer(),
              3);
}

int main() {
    RUN(test_binarytree_balance);
    RUN(test_binarytree_balance_preserves_elements);
    RUN(test_binarytree_balance_empty);
    RUN(test_binarytree_ceiling_incomparable_throws);
    RUN(test_binarytree_ceiling_no_ceiling);
    RUN(test_binarytree_contains_incomparable_throws);
    RUN(test_binarytree_filter);
    RUN(test_binarytree_filter_empty);
    RUN(test_binarytree_floor_ceiling);
    RUN(test_binarytree_floor_incomparable_throws);
    RUN(test_binarytree_from_array);
    RUN(test_binarytree_from_array_incomparable_throws);
    RUN(test_binarytree_height);
    RUN(test_binarytree_inorder);
    RUN(test_binarytree_insert_caps_size);
    RUN(test_binarytree_insert_contains);
    RUN(test_binarytree_insert_incomparable_throws);
    RUN(test_binarytree_is_empty);
    RUN(test_binarytree_level_order);
    RUN(test_binarytree_min_max);
    RUN(test_binarytree_min_max_empty);
    RUN(test_binarytree_module);
    RUN(test_binarytree_partition);
    RUN(test_binarytree_postorder);
    RUN(test_binarytree_predecessor);
    RUN(test_binarytree_predecessor_first);
    RUN(test_binarytree_predecessor_incomparable_throws);
    RUN(test_binarytree_predecessor_not_found);
    RUN(test_binarytree_preorder);
    RUN(test_binarytree_reduce);
    RUN(test_binarytree_reduce_empty);
    RUN(test_binarytree_remove);
    RUN(test_binarytree_remove_incomparable_throws);
    RUN(test_binarytree_successor);
    RUN(test_binarytree_successor_incomparable_throws);
    RUN(test_binarytree_successor_last);
    RUN(test_binarytree_successor_not_found);
    RUN(test_binarytree_to_array);
    RUN(test_binarytree_union);
    RUN(test_binarytree_union_incomparable_throws);
    return SUMMARY();
}
