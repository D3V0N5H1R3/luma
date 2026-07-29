// Standard library tests: KeyValueStore.

#include <cstddef>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/stdlib/collections/keyvaluestore_codec.hpp"
#include "stdlib_test_helpers.hpp"

// ─── Codec layer (keyvaluestore_codec.hpp) ───────────────────────────────────
// The escape/unescape line codec, the .kv parser and the glob matcher form the
// trust boundary behind open/reload/find_by_pattern and are fuzzed directly by
// fuzz/fuzz_keyvaluestore.cpp.  These unit tests pin their contract.

static void test_keyvaluestore_codec_escape_roundtrip() {
    const std::string original = "tab\there\nnewline\\back\\slash";

    ASSERT_EQ(kvs::unescape(kvs::escape(original)), original);
}

static void test_keyvaluestore_codec_escape_renders_specials() {
    // The three special bytes become two-character sequences.
    ASSERT_EQ(kvs::escape("a\tb\nc\\d"), std::string{"a\\tb\\nc\\\\d"});
}

static void test_keyvaluestore_codec_parse_serialize_roundtrip() {
    kvs::StoreEntries entries{};
    entries["key\twith\ttabs"] = "value\nwith\nnewlines";
    entries["back\\slash"] = "plain";
    entries[""] = "empty-key";
    entries["empty-value"] = "";

    ASSERT_TRUE(kvs::parse_store(kvs::serialize_store(entries)) == entries);
}

static void test_keyvaluestore_codec_parse_skips_malformed() {
    // Blank lines and lines without a tab separator are skipped.
    const auto entries = kvs::parse_store("\nno_tab_here\nkey\tvalue\n");

    ASSERT_EQ(entries.size(), std::size_t{1});
    ASSERT_EQ(entries.at("key"), std::string{"value"});
}

static void test_keyvaluestore_codec_parse_empty() {
    ASSERT_EQ(kvs::parse_store("").size(), std::size_t{0});
}

static void test_keyvaluestore_codec_glob_match() {
    ASSERT_TRUE(kvs::glob_match("user_*", "user_alice"));
    ASSERT_TRUE(kvs::glob_match("a?c", "abc"));
    ASSERT_TRUE(kvs::glob_match("*", "anything"));
    ASSERT_TRUE(kvs::glob_match("", ""));
    ASSERT_TRUE(kvs::glob_match("*mid*", "xxmidyy"));

    ASSERT_FALSE(kvs::glob_match("user_*", "session_1"));
    ASSERT_FALSE(kvs::glob_match("a?c", "ac"));
    ASSERT_FALSE(kvs::glob_match("", "x"));
}

// ─── Registration ────────────────────────────────────────────────────────────

static void test_keyvaluestore_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("KeyValueStore.open"));
    ASSERT_TRUE(env->has("KeyValueStore.open_read_only"));
    ASSERT_TRUE(env->has("KeyValueStore.get"));
    ASSERT_TRUE(env->has("KeyValueStore.get_or"));
    ASSERT_TRUE(env->has("KeyValueStore.set"));
    ASSERT_TRUE(env->has("KeyValueStore.remove"));
    ASSERT_TRUE(env->has("KeyValueStore.has"));
    ASSERT_TRUE(env->has("KeyValueStore.set_many"));
    ASSERT_TRUE(env->has("KeyValueStore.get_many"));
    ASSERT_TRUE(env->has("KeyValueStore.keys"));
    ASSERT_TRUE(env->has("KeyValueStore.values"));
    ASSERT_TRUE(env->has("KeyValueStore.to_dictionary"));
    ASSERT_TRUE(env->has("KeyValueStore.count"));
    ASSERT_TRUE(env->has("KeyValueStore.save"));
    ASSERT_TRUE(env->has("KeyValueStore.reload"));
    ASSERT_TRUE(env->has("KeyValueStore.clear"));
    ASSERT_TRUE(env->has("KeyValueStore.destroy"));
    ASSERT_TRUE(env->has("KeyValueStore.find_by_pattern"));
    ASSERT_TRUE(env->has("KeyValueStore.is_read_only"));
}

// ─── Positive: in-memory operations ──────────────────────────────────────────

static void test_keyvaluestore_set_get() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("name", "alice")
        |> Result.unwrap()
        |> KeyValueStore.get("name")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "alice");
}

static void test_keyvaluestore_set_overwrites() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_overwrite.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "first")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "second")
        |> Result.unwrap()
        |> KeyValueStore.get("k")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "second");
}

static void test_keyvaluestore_has() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest2.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("x", "1")
        |> Result.unwrap()
        |> KeyValueStore.has("x")
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_keyvaluestore_has_false() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest3.kv")
        |> Result.unwrap()
        |> KeyValueStore.has("missing")
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_keyvaluestore_get_or_present() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_getor1.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("name", "alice")
        |> Result.unwrap()
        |> KeyValueStore.get_or("name", "fallback")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "alice");
}

static void test_keyvaluestore_get_or_missing_returns_default() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_getor2.kv")
        |> Result.unwrap()
        |> KeyValueStore.get_or("absent", "fallback")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "fallback");
}

static void test_keyvaluestore_remove() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest4.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
        |> Result.unwrap()
        |> KeyValueStore.remove("k")
        |> Result.unwrap()
        |> KeyValueStore.has("k")
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_keyvaluestore_count() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest5.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("a", "1")
        |> Result.unwrap()
        |> KeyValueStore.set("b", "2")
        |> Result.unwrap()
        |> KeyValueStore.count()
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), int64_t{2});
}

static void test_keyvaluestore_clear() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest6.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("a", "1")
        |> Result.unwrap()
        |> KeyValueStore.clear()
        |> Result.unwrap()
        |> KeyValueStore.count()
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), int64_t{0});
}

static void test_keyvaluestore_to_dictionary() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpptest7.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
        |> Result.unwrap()
        |> KeyValueStore.to_dictionary()
    )");

    ASSERT_TRUE(v.is_dictionary());

    const auto* k = v.as_dictionary()->find("k");

    ASSERT_TRUE(k && k->is_string());
    ASSERT_EQ(k->as_string(), "v");
}

static void test_keyvaluestore_keys_count() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_keys.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("x", "1")
        |> Result.unwrap()
        |> KeyValueStore.set("y", "2")
        |> Result.unwrap()
        |> KeyValueStore.keys()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{2});
}

static void test_keyvaluestore_values_single() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_values.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("only", "42")
        |> Result.unwrap()
        |> KeyValueStore.values()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{1});
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "42");
}

static void test_keyvaluestore_set_many_count() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_setmany.kv")
        |> Result.unwrap()
        |> KeyValueStore.set_many({"a": "1", "b": "2", "c": "3"})
        |> Result.unwrap()
        |> KeyValueStore.count()
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), int64_t{3});
}

static void test_keyvaluestore_get_many_skips_missing() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_getmany.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("x", "10")
        |> Result.unwrap()
        |> KeyValueStore.set("y", "20")
        |> Result.unwrap()
        |> KeyValueStore.get_many(["x", "missing"])
    )");

    ASSERT_TRUE(v.is_dictionary());

    const auto* x = v.as_dictionary()->find("x");

    ASSERT_TRUE(x && x->is_string());
    ASSERT_EQ(x->as_string(), "10");
    ASSERT_TRUE(v.as_dictionary()->find("missing") == nullptr);
    ASSERT_TRUE(v.as_dictionary()->find("y") == nullptr);
}

static void test_keyvaluestore_find_by_pattern_wildcard() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_glob.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("user_alice", "admin")
        |> Result.unwrap()
        |> KeyValueStore.set("user_bob", "editor")
        |> Result.unwrap()
        |> KeyValueStore.set("session_1", "active")
        |> Result.unwrap()
        |> KeyValueStore.find_by_pattern("user_*")
    )");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->entries.size(), std::size_t{2});

    const auto* alice = v.as_dictionary()->find("user_alice");

    ASSERT_TRUE(alice && alice->is_string());
    ASSERT_EQ(alice->as_string(), "admin");
    ASSERT_TRUE(v.as_dictionary()->find("session_1") == nullptr);
}

// ─── Positive: read-only mode ────────────────────────────────────────────────

static void test_keyvaluestore_is_read_only_true() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro.kv")
        |> Result.unwrap()
        |> KeyValueStore.is_read_only()
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_keyvaluestore_is_read_only_false() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_rw.kv")
        |> Result.unwrap()
        |> KeyValueStore.is_read_only()
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

// ─── Positive: persistence through the .kv file codec ────────────────────────

static void test_keyvaluestore_save_reopen() {
    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_persist.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
        |> Result.unwrap()
        |> KeyValueStore.save()
    )");

    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_persist.kv")
        |> Result.unwrap()
        |> KeyValueStore.get("k")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "v");

    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_persist.kv")
        |> Result.unwrap()
        |> KeyValueStore.destroy()
    )");
}

static void test_keyvaluestore_reload_from_disk() {
    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_reload.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
        |> Result.unwrap()
        |> KeyValueStore.save()
    )");

    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_reload.kv")
        |> Result.unwrap()
        |> KeyValueStore.reload()
        |> Result.unwrap()
        |> KeyValueStore.get("k")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "v");

    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_reload.kv")
        |> Result.unwrap()
        |> KeyValueStore.destroy()
    )");
}

// Special characters (tab, newline, backslash) must survive an escape →
// write → read → unescape round-trip on disk.  Luma processes \t, \n and \\
// in string literals identically to C++, so the expected value below is the
// same byte sequence on both sides.
static void test_keyvaluestore_special_chars_persist() {
    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_special.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "a\tb\nc\\d")
        |> Result.unwrap()
        |> KeyValueStore.save()
    )");

    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_special.kv")
        |> Result.unwrap()
        |> KeyValueStore.get("k")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "a\tb\nc\\d");

    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_special.kv")
        |> Result.unwrap()
        |> KeyValueStore.destroy()
    )");
}

static void test_keyvaluestore_destroy_removes_file() {
    (void)eval(R"(
        KeyValueStore.open("test_kv_cpp_destroy.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
        |> Result.unwrap()
        |> KeyValueStore.save()
    )");

    const auto destroyed = eval(R"(
        KeyValueStore.open("test_kv_cpp_destroy.kv")
        |> Result.unwrap()
        |> KeyValueStore.destroy()
    )");

    ASSERT_RESULT_SUCCESS(destroyed);

    const auto exists = eval(R"(
        FileSystem.exists("test_kv_cpp_destroy.kv") |> Result.unwrap()
    )");

    ASSERT_TRUE(exists.is_bool());
    ASSERT_FALSE(exists.as_bool());
}

// ─── Negative: missing key yields a failure result ───────────────────────────

static void test_keyvaluestore_get_missing_fails() {
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_getmiss.kv")
        |> Result.unwrap()
        |> KeyValueStore.get("missing")
    )");

    ASSERT_RESULT_FAILURE(v);
}

// ─── Negative: an oversize store file is not slurped into memory ──────────────

static void test_keyvaluestore_open_rejects_oversized_file() {
    // read_store slurps the whole file into memory before parse_store applies
    // its size caps, so a store file larger than the maximum string size must be
    // rejected up front.  An oversize file is treated as unreadable — the store
    // opens empty — instead of being fully materialised.  The file is a valid
    // single entry created under the default cap; lowering the cap afterwards
    // avoids materialising a 256 MB file.
    const std::string kv_content = "key\t" + std::string(64, 'v') + "\n";
    const LumaTempFile file{"_test_kv_oversize.kv", kv_content};
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(16)};

    // The oversize file yields an empty store, so a lookup of the key that is
    // physically present in the file fails.
    const auto v = eval(R"(
        KeyValueStore.open("_test_kv_oversize.kv")
        |> Result.unwrap()
        |> KeyValueStore.get("key")
    )");

    ASSERT_RESULT_FAILURE(v);
}

// ─── Negative: writes on a read-only store fail (no throw, failure result) ────

static void test_keyvaluestore_set_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_set.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("k", "v")
    )");

    ASSERT_RESULT_FAILURE(v);
}

static void test_keyvaluestore_remove_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_remove.kv")
        |> Result.unwrap()
        |> KeyValueStore.remove("k")
    )");

    ASSERT_RESULT_FAILURE(v);
}

static void test_keyvaluestore_clear_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_clear.kv")
        |> Result.unwrap()
        |> KeyValueStore.clear()
    )");

    ASSERT_RESULT_FAILURE(v);
}

static void test_keyvaluestore_set_many_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_setmany.kv")
        |> Result.unwrap()
        |> KeyValueStore.set_many({"a": "1"})
    )");

    ASSERT_RESULT_FAILURE(v);
}

static void test_keyvaluestore_save_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_save.kv")
        |> Result.unwrap()
        |> KeyValueStore.save()
    )");

    ASSERT_RESULT_FAILURE(v);
}

static void test_keyvaluestore_destroy_read_only_fails() {
    const auto v = eval(R"(
        KeyValueStore.open_read_only("test_kv_cpp_ro_destroy.kv")
        |> Result.unwrap()
        |> KeyValueStore.destroy()
    )");

    ASSERT_RESULT_FAILURE(v);
}

// ─── Negative: entry-count cap mirrors parse_store / Dictionary.set ──────────

static void test_keyvaluestore_set_caps_new_keys() {
    // Lower the cap so it can be exercised without inserting millions of keys.
    const LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(2)};

    // Two distinct keys fill the store to the cap; a third NEW key must fail
    // rather than grow past the limit that a later open()/reload() would reject.
    ASSERT_THROWS(eval(R"(
        KeyValueStore.open("test_kv_cpp_cap_set.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("a", "1")
        |> Result.unwrap()
        |> KeyValueStore.set("b", "2")
        |> Result.unwrap()
        |> KeyValueStore.set("c", "3")
    )"));

    // Overwriting an existing key at the cap is still allowed (no new entry).
    const auto v = eval(R"(
        KeyValueStore.open("test_kv_cpp_cap_overwrite.kv")
        |> Result.unwrap()
        |> KeyValueStore.set("a", "1")
        |> Result.unwrap()
        |> KeyValueStore.set("b", "2")
        |> Result.unwrap()
        |> KeyValueStore.set("a", "updated")
        |> Result.unwrap()
        |> KeyValueStore.get("a")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "updated");
}

static void test_keyvaluestore_set_many_caps_size() {
    const LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(2)};

    // A single set_many whose distinct keys exceed the cap must fail, matching
    // the entry-count guard parse_store enforces on the persisted file.
    ASSERT_THROWS(eval(R"(
        KeyValueStore.open("test_kv_cpp_cap_setmany.kv")
        |> Result.unwrap()
        |> KeyValueStore.set_many({"a": "1", "b": "2", "c": "3"})
    )"));
}

// ─── Negative: type errors raise RuntimeError ────────────────────────────────

static void test_keyvaluestore_open_non_string_throws() {
    ASSERT_THROWS(eval("KeyValueStore.open(42)"));
}

static void test_keyvaluestore_get_non_store_throws() {
    ASSERT_THROWS(eval(R"(KeyValueStore.get(42, "k"))"));
}

static void test_keyvaluestore_has_non_store_throws() {
    ASSERT_THROWS(eval(R"(KeyValueStore.has("not a store", "k"))"));
}

static void test_keyvaluestore_set_non_string_key_throws() {
    ASSERT_THROWS(eval(R"(
        KeyValueStore.open("test_kv_cpp_badkey.kv")
        |> Result.unwrap()
        |> KeyValueStore.set(123, "v")
    )"));
}

static void test_keyvaluestore_set_many_non_dict_throws() {
    ASSERT_THROWS(eval(R"(
        KeyValueStore.open("test_kv_cpp_baddict.kv")
        |> Result.unwrap()
        |> KeyValueStore.set_many("not a dict")
    )"));
}

int main() {
    // Codec layer.
    RUN(test_keyvaluestore_codec_escape_roundtrip);
    RUN(test_keyvaluestore_codec_escape_renders_specials);
    RUN(test_keyvaluestore_codec_parse_serialize_roundtrip);
    RUN(test_keyvaluestore_codec_parse_skips_malformed);
    RUN(test_keyvaluestore_codec_parse_empty);
    RUN(test_keyvaluestore_codec_glob_match);

    // Registration.
    RUN(test_keyvaluestore_module);

    // Positive: in-memory operations.
    RUN(test_keyvaluestore_set_get);
    RUN(test_keyvaluestore_set_overwrites);
    RUN(test_keyvaluestore_has);
    RUN(test_keyvaluestore_has_false);
    RUN(test_keyvaluestore_get_or_present);
    RUN(test_keyvaluestore_get_or_missing_returns_default);
    RUN(test_keyvaluestore_remove);
    RUN(test_keyvaluestore_count);
    RUN(test_keyvaluestore_clear);
    RUN(test_keyvaluestore_to_dictionary);
    RUN(test_keyvaluestore_keys_count);
    RUN(test_keyvaluestore_values_single);
    RUN(test_keyvaluestore_set_many_count);
    RUN(test_keyvaluestore_get_many_skips_missing);
    RUN(test_keyvaluestore_find_by_pattern_wildcard);

    // Positive: read-only mode.
    RUN(test_keyvaluestore_is_read_only_true);
    RUN(test_keyvaluestore_is_read_only_false);

    // Positive: persistence.
    RUN(test_keyvaluestore_save_reopen);
    RUN(test_keyvaluestore_reload_from_disk);
    RUN(test_keyvaluestore_special_chars_persist);
    RUN(test_keyvaluestore_destroy_removes_file);

    // Negative: failure results.
    RUN(test_keyvaluestore_get_missing_fails);
    RUN(test_keyvaluestore_open_rejects_oversized_file);
    RUN(test_keyvaluestore_set_read_only_fails);
    RUN(test_keyvaluestore_remove_read_only_fails);
    RUN(test_keyvaluestore_clear_read_only_fails);
    RUN(test_keyvaluestore_set_many_read_only_fails);
    RUN(test_keyvaluestore_save_read_only_fails);
    RUN(test_keyvaluestore_destroy_read_only_fails);

    // Negative: entry-count cap.
    RUN(test_keyvaluestore_set_caps_new_keys);
    RUN(test_keyvaluestore_set_many_caps_size);

    // Negative: type errors.
    RUN(test_keyvaluestore_open_non_string_throws);
    RUN(test_keyvaluestore_get_non_store_throws);
    RUN(test_keyvaluestore_has_non_store_throws);
    RUN(test_keyvaluestore_set_non_string_key_throws);
    RUN(test_keyvaluestore_set_many_non_dict_throws);

    return SUMMARY();
}
