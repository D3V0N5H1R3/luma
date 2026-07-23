// Standard library tests: Dictionary.
//
// Covers all 28 Dictionary functions with positive cases plus negative cases
// (missing keys, non-string keys, malformed inputs, callback errors, and the
// deep_merge recursion-depth and circular-reference guards).
// eval() runs the unchecked pipeline, so the module's runtime guards are
// reachable: key-type and malformed-input guards throw RuntimeError, while
// callback errors are converted to failure results by apply_with_error_handling.

#include "stdlib_test_helpers.hpp"

// ─── has ──────────────────────────────────────────────────────────────

static void test_dictionary_has() {
    ASSERT_EQ(eval("Dictionary.has({\"a\": 1}, \"a\")").as_bool(), true);
    ASSERT_EQ(eval("Dictionary.has({\"a\": 1}, \"b\")").as_bool(), false);
    ASSERT_EQ(eval("Dictionary.has({}, \"a\")").as_bool(), false);
}

static void test_dictionary_has_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.has({\"a\": 1}, 5)"));
}

// ─── get / get_or ─────────────────────────────────────────────────────

static void test_dictionary_get() {
    ASSERT_EVAL_INT("Dictionary.get({\"a\": 42}, \"a\")", 42);
}

static void test_dictionary_get_missing_key() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.get({\"a\": 1}, \"z\")"));
}

static void test_dictionary_get_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.get({\"a\": 1}, 5)"));
}

static void test_dictionary_get_or() {
    ASSERT_EQ(eval("Dictionary.get_or({\"a\": 1}, \"a\", 0)").as_integer(), 1);
    ASSERT_EQ(eval("Dictionary.get_or({\"a\": 1}, \"missing\", 42)").as_integer(), 42);
    ASSERT_EQ(eval("Dictionary.get_or({}, \"x\", 7)").as_integer(), 7);
}

static void test_dictionary_get_or_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.get_or({\"a\": 1}, 5, 0)"));
}

// ─── set ──────────────────────────────────────────────────────────────

static void test_dictionary_set() {
    // The inserted key is present in the returned dictionary.
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.set({\"a\": 1}, \"b\", 2), \"b\", 0)").as_integer(), 2);
    ASSERT_EQ(eval("Dictionary.length(Dictionary.set({\"a\": 1}, \"b\", 2))").as_integer(), 2);
}

static void test_dictionary_set_overwrites_existing() {
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.set({\"a\": 1}, \"a\", 9), \"a\", 0)").as_integer(), 9);
    ASSERT_EQ(eval("Dictionary.length(Dictionary.set({\"a\": 1}, \"a\", 9))").as_integer(), 1);
}

static void test_dictionary_set_preserves_existing() {
    // Adding a new key preserves the existing entries unchanged.
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.set({\"a\": 1}, \"b\", 2), \"a\", 0)").as_integer(), 1);
    ASSERT_EQ(eval("Dictionary.has(Dictionary.set({\"a\": 1}, \"b\", 2), \"a\")").as_bool(), true);
}

static void test_dictionary_set_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.set({\"a\": 1}, 5, 2)"));
}

// ─── remove ───────────────────────────────────────────────────────────

static void test_dictionary_remove() {
    ASSERT_EQ(
        eval("Dictionary.length(Dictionary.remove({\"a\": 1, \"b\": 2}, \"a\"))").as_integer(), 1);
    ASSERT_EQ(
        eval("Dictionary.has(Dictionary.remove({\"a\": 1, \"b\": 2}, \"a\"), \"a\")").as_bool(),
        false);
}

static void test_dictionary_remove_missing_key() {
    // Removing an absent key is a no-op rather than an error.
    ASSERT_EQ(eval("Dictionary.length(Dictionary.remove({\"a\": 1}, \"z\"))").as_integer(), 1);
}

static void test_dictionary_remove_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.remove({\"a\": 1}, 5)"));
}

// ─── length / is_empty ────────────────────────────────────────────────

static void test_dictionary_length() {
    ASSERT_EQ(eval("Dictionary.length({\"a\": 1, \"b\": 2})").as_integer(), 2);
    ASSERT_EQ(eval("Dictionary.length({})").as_integer(), 0);
}

static void test_dictionary_is_empty() {
    ASSERT_EQ(eval("Dictionary.is_empty({})").as_bool(), true);
    ASSERT_EQ(eval("Dictionary.is_empty({\"a\": 1})").as_bool(), false);
}

// ─── keys / values ────────────────────────────────────────────────────

static void test_dictionary_keys() {
    const auto v = eval("Dictionary.keys({\"a\": 1, \"b\": 2})");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
}

static void test_dictionary_keys_empty() {
    const auto v = eval("Dictionary.keys({})");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 0U);
}

static void test_dictionary_values() {
    const auto v = eval("Dictionary.values({\"a\": 1, \"b\": 2})");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
}

// ─── merge ────────────────────────────────────────────────────────────

static void test_dictionary_merge() {
    ASSERT_EQ(
        eval("Dictionary.length(Dictionary.merge({\"a\": 1, \"b\": 2}, {\"c\": 3}))").as_integer(),
        3);
}

static void test_dictionary_merge_overlay_wins() {
    ASSERT_EQ(eval("Dictionary.get_or("
                   "Dictionary.merge({\"a\": 1, \"b\": 2}, {\"b\": 9, \"c\": 3}), \"b\", 0)")
                  .as_integer(),
              9);
}

// ─── each ─────────────────────────────────────────────────────────────

static void test_dictionary_each() {
    const auto ok = eval("Dictionary.each({\"a\": 1}, (string k, integer v) -> none)");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_null());
}

static void test_dictionary_each_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.each({\"a\": 1}, (string k, integer v) -> v / 0)"));
}

// ─── from_keys ────────────────────────────────────────────────────────

static void test_dictionary_from_keys() {
    ASSERT_EQ(
        eval("Dictionary.length(Dictionary.from_keys([\"a\", \"b\", \"c\"], 0))").as_integer(), 3);
    ASSERT_EQ(eval("Dictionary.get_or(Dictionary.from_keys([\"a\"], 7), \"a\", -1)").as_integer(),
              7);
}

static void test_dictionary_from_keys_empty() {
    ASSERT_EQ(eval("Dictionary.length(Dictionary.from_keys([], 0))").as_integer(), 0);
}

static void test_dictionary_from_keys_non_string_throws() {
    ASSERT_THROWS(eval("Dictionary.from_keys([1, 2], 0)"));
}

// ─── to_array ─────────────────────────────────────────────────────────

static void test_dictionary_to_array() {
    const auto v = eval("Dictionary.to_array({\"x\": 1})");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 1U);

    const auto& elem = (*v.as_array()->elements)[0];

    ASSERT_TRUE(elem.is_record());
    ASSERT_EQ(elem.as_record()->type_name, std::string{"KeyValue"});
    ASSERT_EQ(elem.as_record()->fields.size(), 2U);

    const auto* key = elem.as_record()->find_field("key");
    const auto* value = elem.as_record()->find_field("value");
    ASSERT_TRUE(key != nullptr);
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(key->as_string(), std::string{"x"});
    ASSERT_EQ(value->as_integer(), 1);
}

// ─── to_entries / from_entries ────────────────────────────────────────

static void test_dictionary_to_entries() {
    const auto v = eval("Dictionary.to_entries({\"x\": 10})");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 1U);

    const auto& elem = (*v.as_array()->elements)[0];

    ASSERT_TRUE(elem.is_tuple());
    ASSERT_EQ(elem.as_tuple()->elements.size(), 2U);
    ASSERT_EQ(elem.as_tuple()->elements[0].as_string(), std::string("x"));
    ASSERT_EQ(elem.as_tuple()->elements[1].as_integer(), 10);
}

static void test_dictionary_from_entries() {
    ASSERT_EQ(
        eval("Dictionary.length(Dictionary.from_entries([(\"a\", 1), (\"b\", 2)]))").as_integer(),
        2);
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.from_entries([(\"a\", 1)]), \"a\", 0)").as_integer(), 1);
}

static void test_dictionary_from_entries_non_tuple_throws() {
    ASSERT_THROWS(eval("Dictionary.from_entries([1, 2])"));
}

static void test_dictionary_from_entries_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.from_entries([(5, 1)])"));
}

static void test_dictionary_from_entries_wrong_arity_throws() {
    // Each entry must be a (key, value) pair; tuples of any other arity are
    // rejected rather than silently truncated.
    ASSERT_THROWS(eval("Dictionary.from_entries([(\"a\", 1, 2)])"));
}

// ─── invert ───────────────────────────────────────────────────────────

static void test_dictionary_invert() {
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.invert({\"a\": \"x\", \"b\": \"y\"}), \"x\", \"\")")
            .as_string(),
        std::string("a"));
}

static void test_dictionary_invert_non_string_values() {
    // Non-string values are stringified to form the inverted keys.
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.invert({\"a\": 1, \"b\": 2}), \"2\", \"\")").as_string(),
        std::string("b"));
}

// ─── deep_merge ───────────────────────────────────────────────────────

static void test_dictionary_deep_merge_shallow() {
    ASSERT_EQ(
        eval("Dictionary.get_or(Dictionary.deep_merge({\"a\": \"1\"}, {\"b\": \"2\"}), \"b\", "
             "\"\")")
            .as_string(),
        std::string("2"));
}

static void test_dictionary_deep_merge_nested() {
    // Nested dictionaries under the same key are merged recursively rather
    // than the overlay replacing the base wholesale.
    const auto v = eval("Dictionary.deep_merge("
                        "{\"outer\": {\"x\": 1, \"y\": 2}},"
                        "{\"outer\": {\"y\": 9, \"z\": 3}})");

    ASSERT_TRUE(v.is_dictionary());

    const auto* outer = v.as_dictionary()->find("outer");

    ASSERT_TRUE(outer != nullptr);
    ASSERT_TRUE(outer->is_dictionary());
    ASSERT_EQ(outer->as_dictionary()->entries.size(), 3U);
}

static void test_dictionary_deep_merge_shared_substructure() {
    // base.x and base.y reference the SAME nested dictionary. deep_merge must
    // merge each branch independently without falsely reporting the shared
    // (non-cyclic) reference as a circular reference — a regression guard for
    // the VisitedGuard scope-exit cleanup.
    const auto v = eval("shared = {\"p\": 1}\n"
                        "base = {\"x\": shared, \"y\": shared}\n"
                        "over = {\"x\": {\"q\": 2}, \"y\": {\"r\": 3}}\n"
                        "Dictionary.deep_merge(base, over)");

    ASSERT_TRUE(v.is_dictionary());

    const auto* x = v.as_dictionary()->find("x");
    const auto* y = v.as_dictionary()->find("y");

    ASSERT_TRUE(x != nullptr && x->is_dictionary());
    ASSERT_TRUE(y != nullptr && y->is_dictionary());
    ASSERT_EQ(x->as_dictionary()->entries.size(), 2U);
    ASSERT_EQ(y->as_dictionary()->entries.size(), 2U);
    ASSERT_TRUE(x->as_dictionary()->find("q") != nullptr);
    ASSERT_TRUE(y->as_dictionary()->find("r") != nullptr);
}

static void test_dictionary_deep_merge_self_succeeds() {
    // Merging a dictionary with itself is NOT a circular reference: the same
    // underlying object appears on both sides, but the structure is finite and
    // acyclic, so the merge terminates and returns the dictionary's own
    // contents. This previously threw "circular reference detected" because a
    // single shared visited-set collided when base and overlay were the same
    // pointer — a regression guard for the split base/overlay visited-sets.
    const auto v = eval("d = {\"k\": {\"b\": 1}}\nDictionary.deep_merge(d, d)");

    ASSERT_TRUE(v.is_dictionary());

    const auto* k = v.as_dictionary()->find("k");

    ASSERT_TRUE(k != nullptr && k->is_dictionary());
    ASSERT_TRUE(k->as_dictionary()->find("b") != nullptr);
}

static void test_dictionary_deep_merge_max_depth_throws() {
    // Build two independent dictionaries nested far deeper than the merge
    // recursion limit using a chain of shallow single-assignment statements
    // (each new binding wraps the previous one). Every *expression* stays
    // shallow, so the parser's own nesting guard is not what fires; merging the
    // two deep chains then exercises Dictionary.deep_merge's maximum-depth guard.
    std::string source = "a0 = {\"v\": 1}\nb0 = {\"v\": 2}\n";

    for (int i = 1; i <= 150; ++i) {
        const auto prev = std::to_string(i - 1);
        const auto cur = std::to_string(i);
        source += "a" + cur + " = {\"k\": a" + prev + "}\n";
        source += "b" + cur + " = {\"k\": b" + prev + "}\n";
    }

    source += "Dictionary.deep_merge(a150, b150)";

    try {
        eval(source);
        ASSERT_TRUE(false);
    } catch (const RuntimeError& e) {
        const std::string message{e.what()};
        ASSERT_TRUE(message.find("deep_merge") != std::string::npos);
        ASSERT_TRUE(message.find("depth") != std::string::npos);
    }
}

// ─── map_values ───────────────────────────────────────────────────────

static void test_dictionary_map_values() {
    const auto ok = eval("Dictionary.map_values({\"a\": 1, \"b\": 2}, (integer v) -> v * 10)");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_dictionary());
    ASSERT_EQ(ok.as_result()->owned_inner->as_dictionary()->entries.size(), 2U);
}

static void test_dictionary_map_values_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.map_values({\"a\": 1}, (integer v) -> v / 0)"));
}

// ─── filter ───────────────────────────────────────────────────────────

static void test_dictionary_filter() {
    const auto ok = eval("Dictionary.filter({\"a\": 1, \"b\": 5}, (string k, integer v) -> v > 2)");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_dictionary());
    ASSERT_EQ(ok.as_result()->owned_inner->as_dictionary()->entries.size(), 1U);
}

static void test_dictionary_filter_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.filter({\"a\": 1}, (string k, integer v) -> v / 0)"));
}

// ─── has_value ────────────────────────────────────────────────────────

static void test_dictionary_has_value() {
    ASSERT_EQ(eval("Dictionary.has_value({\"a\": 1, \"b\": 2}, 2)").as_bool(), true);
    ASSERT_EQ(eval("Dictionary.has_value({\"a\": 1, \"b\": 2}, 99)").as_bool(), false);
}

// ─── reduce ───────────────────────────────────────────────────────────

static void test_dictionary_reduce() {
    ASSERT_EVAL_INT("Dictionary.reduce({\"a\": 1, \"b\": 2, \"c\": 3}, 0, "
                    "(integer acc, string k, integer v) -> acc + v)",
                    6);
}

static void test_dictionary_reduce_empty() {
    ASSERT_EVAL_INT("Dictionary.reduce({}, 42, "
                    "(integer acc, string k, integer v) -> acc + v)",
                    42);
}

static void test_dictionary_reduce_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.reduce({\"a\": 1}, 0, "
                               "(integer acc, string k, integer v) -> v / 0)"));
}

// ─── map ──────────────────────────────────────────────────────────────

static void test_dictionary_map() {
    const auto ok = eval("Dictionary.map({\"a\": 1, \"b\": 2}, (string k, integer v) -> v * 10)");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_dictionary());
    ASSERT_EQ(ok.as_result()->owned_inner->as_dictionary()->entries.size(), 2U);
}

static void test_dictionary_map_empty() {
    const auto ok = eval("Dictionary.map({}, (string k, integer v) -> v)");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_dictionary());
    ASSERT_EQ(ok.as_result()->owned_inner->as_dictionary()->entries.size(), 0U);
}

static void test_dictionary_map_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.map({\"a\": 1}, (string k, integer v) -> v / 0)"));
}

// ─── partition ────────────────────────────────────────────────────────

static void test_dictionary_partition() {
    const auto v = eval("Dictionary.partition("
                        "{\"a\": 1, \"b\": 2, \"c\": 3},"
                        "(string k, integer v) -> v > 1)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_dictionary()->entries.size(), 2U);
    ASSERT_EQ(tup[1].as_dictionary()->entries.size(), 1U);
}

static void test_dictionary_partition_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.partition({\"a\": 1}, (string k, integer v) -> v / 0)"));
}

// ─── pick ─────────────────────────────────────────────────────────────

static void test_dictionary_pick() {
    const auto v = eval("Dictionary.pick({\"a\": 1, \"b\": 2, \"c\": 3}, [\"a\", \"c\"])");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->entries.size(), 2U);
    ASSERT_TRUE(v.as_dictionary()->find("a") != nullptr);
    ASSERT_TRUE(v.as_dictionary()->find("b") == nullptr);
}

static void test_dictionary_pick_missing_keys() {
    // Keys absent from the source are silently skipped.
    ASSERT_EQ(eval("Dictionary.length(Dictionary.pick({\"a\": 1}, [\"a\", \"z\"]))").as_integer(),
              1);
}

static void test_dictionary_pick_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.pick({\"a\": 1}, [1])"));
}

// ─── omit ─────────────────────────────────────────────────────────────

static void test_dictionary_omit() {
    const auto v = eval("Dictionary.omit({\"a\": 1, \"b\": 2, \"c\": 3}, [\"b\"])");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->entries.size(), 2U);
    ASSERT_TRUE(v.as_dictionary()->find("b") == nullptr);
}

static void test_dictionary_omit_missing_keys() {
    ASSERT_EQ(
        eval("Dictionary.length(Dictionary.omit({\"a\": 1, \"b\": 2}, [\"z\"]))").as_integer(), 2);
}

static void test_dictionary_omit_non_string_key_throws() {
    ASSERT_THROWS(eval("Dictionary.omit({\"a\": 1}, [1])"));
}

// ─── find ─────────────────────────────────────────────────────────────

static void test_dictionary_find() {
    const auto v =
        eval("Dictionary.find({\"a\": 1, \"b\": 2}, (string k, integer val) -> val == 2)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_string(), std::string("b"));
    ASSERT_EQ(tup[1].as_integer(), 2);
}

static void test_dictionary_find_not_found() {
    ASSERT_RESULT_FAILURE(
        eval("Dictionary.find({\"a\": 1}, (string k, integer val) -> val > 100)"));
}

static void test_dictionary_find_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.find({\"a\": 1}, (string k, integer val) -> val / 0)"));
}

// ─── count ────────────────────────────────────────────────────────────

static void test_dictionary_count() {
    ASSERT_EVAL_INT("Dictionary.count({\"a\": 1, \"b\": 2, \"c\": 3, \"d\": 4}, "
                    "(string k, integer val) -> val > 2)",
                    2);
}

static void test_dictionary_count_none_match() {
    ASSERT_EVAL_INT("Dictionary.count({\"a\": 1}, (string k, integer val) -> val > 100)", 0);
}

static void test_dictionary_count_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Dictionary.count({\"a\": 1}, (string k, integer val) -> val / 0)"));
}

// ─── flip ─────────────────────────────────────────────────────────────

static void test_dictionary_flip() {
    const auto v = eval(R"(Dictionary.flip({"a": "1", "b": "2"}))");

    ASSERT_RESULT_SUCCESS(v);

    const auto& dict = v.as_result()->owned_inner->as_dictionary();

    ASSERT_TRUE(dict->find("1") != nullptr);
    ASSERT_TRUE(dict->find("2") != nullptr);
}

static void test_dictionary_flip_non_string_value_fails() {
    // flip requires string values; integer values yield a failure result.
    ASSERT_RESULT_FAILURE(eval("Dictionary.flip({\"a\": 1})"));
}

// ─── registration ─────────────────────────────────────────────────────

static void test_dictionary_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Dictionary.has"));
    ASSERT_TRUE(env->has("Dictionary.get"));
    ASSERT_TRUE(env->has("Dictionary.get_or"));
    ASSERT_TRUE(env->has("Dictionary.set"));
    ASSERT_TRUE(env->has("Dictionary.remove"));
    ASSERT_TRUE(env->has("Dictionary.keys"));
    ASSERT_TRUE(env->has("Dictionary.values"));
    ASSERT_TRUE(env->has("Dictionary.length"));
    ASSERT_TRUE(env->has("Dictionary.merge"));
    ASSERT_TRUE(env->has("Dictionary.deep_merge"));
    ASSERT_TRUE(env->has("Dictionary.from_keys"));
    ASSERT_TRUE(env->has("Dictionary.from_entries"));
    ASSERT_TRUE(env->has("Dictionary.to_entries"));
    ASSERT_TRUE(env->has("Dictionary.invert"));
    ASSERT_TRUE(env->has("Dictionary.has_value"));
    ASSERT_TRUE(env->has("Dictionary.pick"));
    ASSERT_TRUE(env->has("Dictionary.omit"));
    ASSERT_TRUE(env->has("Dictionary.find"));
    ASSERT_TRUE(env->has("Dictionary.count"));
    ASSERT_TRUE(env->has("Dictionary.flip"));
}

int main() {
    RUN(test_dictionary_has);
    RUN(test_dictionary_has_non_string_key_throws);
    RUN(test_dictionary_get);
    RUN(test_dictionary_get_missing_key);
    RUN(test_dictionary_get_non_string_key_throws);
    RUN(test_dictionary_get_or);
    RUN(test_dictionary_get_or_non_string_key_throws);
    RUN(test_dictionary_set);
    RUN(test_dictionary_set_overwrites_existing);
    RUN(test_dictionary_set_preserves_existing);
    RUN(test_dictionary_set_non_string_key_throws);
    RUN(test_dictionary_remove);
    RUN(test_dictionary_remove_missing_key);
    RUN(test_dictionary_remove_non_string_key_throws);
    RUN(test_dictionary_length);
    RUN(test_dictionary_is_empty);
    RUN(test_dictionary_keys);
    RUN(test_dictionary_keys_empty);
    RUN(test_dictionary_values);
    RUN(test_dictionary_merge);
    RUN(test_dictionary_merge_overlay_wins);
    RUN(test_dictionary_each);
    RUN(test_dictionary_each_callback_error);
    RUN(test_dictionary_from_keys);
    RUN(test_dictionary_from_keys_empty);
    RUN(test_dictionary_from_keys_non_string_throws);
    RUN(test_dictionary_to_array);
    RUN(test_dictionary_to_entries);
    RUN(test_dictionary_from_entries);
    RUN(test_dictionary_from_entries_non_tuple_throws);
    RUN(test_dictionary_from_entries_non_string_key_throws);
    RUN(test_dictionary_from_entries_wrong_arity_throws);
    RUN(test_dictionary_invert);
    RUN(test_dictionary_invert_non_string_values);
    RUN(test_dictionary_deep_merge_shallow);
    RUN(test_dictionary_deep_merge_nested);
    RUN(test_dictionary_deep_merge_shared_substructure);
    RUN(test_dictionary_deep_merge_self_succeeds);
    RUN(test_dictionary_deep_merge_max_depth_throws);
    RUN(test_dictionary_map_values);
    RUN(test_dictionary_map_values_callback_error);
    RUN(test_dictionary_filter);
    RUN(test_dictionary_filter_callback_error);
    RUN(test_dictionary_has_value);
    RUN(test_dictionary_reduce);
    RUN(test_dictionary_reduce_empty);
    RUN(test_dictionary_reduce_callback_error);
    RUN(test_dictionary_map);
    RUN(test_dictionary_map_empty);
    RUN(test_dictionary_map_callback_error);
    RUN(test_dictionary_partition);
    RUN(test_dictionary_partition_callback_error);
    RUN(test_dictionary_pick);
    RUN(test_dictionary_pick_missing_keys);
    RUN(test_dictionary_pick_non_string_key_throws);
    RUN(test_dictionary_omit);
    RUN(test_dictionary_omit_missing_keys);
    RUN(test_dictionary_omit_non_string_key_throws);
    RUN(test_dictionary_find);
    RUN(test_dictionary_find_not_found);
    RUN(test_dictionary_find_callback_error);
    RUN(test_dictionary_count);
    RUN(test_dictionary_count_none_match);
    RUN(test_dictionary_count_callback_error);
    RUN(test_dictionary_flip);
    RUN(test_dictionary_flip_non_string_value_fails);
    RUN(test_dictionary_module);

    return SUMMARY();
}
