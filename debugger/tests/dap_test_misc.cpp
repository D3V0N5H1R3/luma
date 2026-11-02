// DAP miscellaneous tests — LRU cache model, working directory, exception dedup.

#include <filesystem>
#include <iostream>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/lru_cache.hpp"
#include "compiled_breakpoint.hpp"
#include "dap_error_handler.hpp"
#include "dap_feature_manager.hpp"
#include "dap_helpers.hpp"
#include "debug_stream_utils.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"

namespace {

// ─── Working directory error reporting ─────────────────────────────

void test_working_dir_error_detected() {
    // Verify that std::filesystem::current_path produces an error for a
    // non-existent path — the production code now checks this error_code
    // and emits a warning to the user.
    std::error_code ec;
    std::filesystem::current_path("/nonexistent_path_xyz_12345", ec);
    ASSERT_TRUE(static_cast<bool>(ec));
    ASSERT_FALSE(ec.message().empty());
}

void test_working_dir_valid_no_error() {
    // A valid directory should not produce an error.
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    ASSERT_FALSE(static_cast<bool>(ec));
    ASSERT_FALSE(cwd.empty());
}

// ─── Value formatting ──────────────────────────────────────────────

// Regression: format_value_hex claimed to return the original string on
// failure but returned "<formatting error>", and rendered negatives as "0x-5".
void test_format_value_hex() {
    ASSERT_EQ(luma::dap::format_value_hex("255"), std::string("0xff"));
    ASSERT_EQ(luma::dap::format_value_hex("0"), std::string("0x0"));
    ASSERT_EQ(luma::dap::format_value_hex("-42"), std::string("-0x2a"));
    ASSERT_EQ(luma::dap::format_value_hex("-9223372036854775808"),
              std::string("-0x8000000000000000"));
    ASSERT_EQ(luma::dap::format_value_hex("hello"), std::string("hello"));
    ASSERT_EQ(luma::dap::format_value_hex("12abc"), std::string("12abc"));
}

// ─── Logpoint template parsing ─────────────────────────────────────

// One parser serves validation and runtime interpolation, so `\{` is a literal
// brace in both (validation used to treat it as the start of an expression).
void test_parse_log_template() {
    const auto parts = luma::dap::parse_log_template(R"(x={x} \{literal} {y + 1} tail {open)");

    ASSERT_EQ(parts.size(), 5U);
    ASSERT_FALSE(parts[0].is_expression);
    ASSERT_EQ(parts[0].text, std::string("x="));
    ASSERT_TRUE(parts[1].is_expression);
    ASSERT_EQ(parts[1].text, std::string("x"));
    ASSERT_FALSE(parts[2].is_expression);
    ASSERT_EQ(parts[2].text, std::string(" {literal} "));
    ASSERT_TRUE(parts[3].is_expression);
    ASSERT_EQ(parts[3].text, std::string("y + 1"));
    ASSERT_FALSE(parts[4].is_expression);
    ASSERT_EQ(parts[4].text, std::string(" tail {open"));
}

// ─── Program output buffering ──────────────────────────────────────

// Regression: the line buffer ignored std::flush, so a newline-less prompt
// stayed buffered until the next newline (or the end of the program).
void test_line_buffer_flushes_partial_line_on_sync() {
    std::vector<std::string> flushed;
    luma::dap::LineBufferedStreamBuf buffer{
        [&flushed](const std::string& text) { flushed.push_back(text); }};
    std::ostream stream{&buffer};

    stream << "one\ntwo";
    ASSERT_EQ(flushed.size(), 1U);
    ASSERT_EQ(flushed[0], std::string("one\n"));

    stream << std::flush;
    ASSERT_EQ(flushed.size(), 2U);
    ASSERT_EQ(flushed[1], std::string("two"));
}

// ─── Capability negotiation ────────────────────────────────────────

// Only implemented features are advertised: exceptionOptions and stepInTargets
// were advertised without being implemented, and `supportsInvalidatedEvent` is
// a client capability, not a server one.  Completions is implemented.
void test_capabilities_match_implemented_features() {
    luma::dap::FeatureManager features;
    features.receive_client_capabilities(
        luma::json::JsonValue(luma::json::JsonValue::ObjectType{}));
    const auto caps = features.build_capabilities_json();

    ASSERT_FALSE(caps.has("supportsExceptionOptions"));
    ASSERT_FALSE(caps.has("supportsStepInTargetsRequest"));
    ASSERT_FALSE(caps.has("supportsInvalidatedEvent"));
    ASSERT_TRUE(caps["supportsCompletionsRequest"].as_bool());
    ASSERT_TRUE(caps["supportsLogPoints"].as_bool());
}

// ─── Expression cache LRU eviction (modeled) ──────────────────────
// The ExpressionEvaluator's LRU cache is private and requires a VM to
// populate, so we model the same algorithm to verify correctness.

class LruCacheModel {
public:
    explicit LruCacheModel(std::size_t max_size) : max_size_(max_size) {}

    void insert(const std::string& key) {
        auto it = index_.find(key);

        if (it != index_.end()) {
            // Move to front (most recently used).
            order_.splice(order_.begin(), order_, it->second);
            return;
        }

        // Evict least recently used entries if cache is full.
        while (entries_.size() >= max_size_ && !order_.empty()) {
            const auto& oldest = order_.back();
            entries_.erase(oldest);
            index_.erase(oldest);
            order_.pop_back();
        }

        entries_.insert(key);
        order_.push_front(key);
        index_[key] = order_.begin();
    }

    void access(const std::string& key) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            order_.splice(order_.begin(), order_, it->second);
        }
    }

    [[nodiscard]] bool contains(const std::string& key) const {
        return entries_.count(key) > 0;
    }

    [[nodiscard]] std::size_t size() const {
        return entries_.size();
    }

    // Returns the key at the back of the LRU list (least recently used).
    [[nodiscard]] std::string lru_back() const {
        return order_.back();
    }

    // Returns the key at the front of the LRU list (most recently used).
    [[nodiscard]] std::string mru_front() const {
        return order_.front();
    }

    void clear() {
        entries_.clear();
        order_.clear();
        index_.clear();
    }

private:
    std::size_t max_size_;
    std::set<std::string> entries_;
    std::list<std::string> lru_order_; // front = MRU, back = LRU
    std::list<std::string>& order_ = lru_order_;
    std::unordered_map<std::string, std::list<std::string>::iterator> index_;
};

void test_lru_cache_max_size_enforcement() {
    // Model with max_size = 4.
    LruCacheModel cache(4);
    cache.insert("a");
    cache.insert("b");
    cache.insert("c");
    cache.insert("d");
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(4));

    // Inserting a 5th entry should evict the oldest.
    cache.insert("e");
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(4));
    ASSERT_FALSE(cache.contains("a")); // "a" was evicted.
    ASSERT_TRUE(cache.contains("e"));
}

void test_lru_cache_eviction_order() {
    LruCacheModel cache(3);
    cache.insert("x");
    cache.insert("y");
    cache.insert("z");

    // LRU order: z (MRU), y, x (LRU).
    ASSERT_EQ(cache.lru_back(), "x");
    ASSERT_EQ(cache.mru_front(), "z");

    // Inserting "w" should evict "x".
    cache.insert("w");
    ASSERT_FALSE(cache.contains("x"));
    ASSERT_TRUE(cache.contains("y"));
    ASSERT_TRUE(cache.contains("z"));
    ASSERT_TRUE(cache.contains("w"));
}

void test_lru_cache_access_refreshes_position() {
    LruCacheModel cache(3);
    cache.insert("a");
    cache.insert("b");
    cache.insert("c");

    // Access "a" to move it to MRU.
    cache.access("a");
    ASSERT_EQ(cache.mru_front(), "a");
    ASSERT_EQ(cache.lru_back(), "b"); // "b" is now LRU.

    // Insert "d" — should evict "b" (LRU), not "a".
    cache.insert("d");
    ASSERT_TRUE(cache.contains("a"));
    ASSERT_FALSE(cache.contains("b"));
    ASSERT_TRUE(cache.contains("c"));
    ASSERT_TRUE(cache.contains("d"));
}

void test_lru_cache_duplicate_insert_refreshes() {
    LruCacheModel cache(3);
    cache.insert("a");
    cache.insert("b");
    cache.insert("c");

    // Re-insert "a" to refresh it.
    cache.insert("a");
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(cache.mru_front(), "a");
}

void test_lru_cache_clear() {
    LruCacheModel cache(3);
    cache.insert("a");
    cache.insert("b");
    cache.clear();
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(0));
    ASSERT_FALSE(cache.contains("a"));
}

void test_lru_cache_single_element() {
    LruCacheModel cache(1);
    cache.insert("only");
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(cache.contains("only"));

    // Inserting another evicts the first.
    cache.insert("replacement");
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
    ASSERT_FALSE(cache.contains("only"));
    ASSERT_TRUE(cache.contains("replacement"));
}

// ─── Real LruCache tests (core/common/lru_cache.hpp) ───────────────
// These test the actual template used in production, not just the model.

void test_real_lru_cache_put_and_get() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);
    (void)cache.put("c", 3);

    ASSERT_EQ(cache.size(), static_cast<std::size_t>(3));

    auto* val = cache.get("a");
    ASSERT_TRUE(val != nullptr);
    ASSERT_EQ(*val, 1);

    val = cache.get("b");
    ASSERT_TRUE(val != nullptr);
    ASSERT_EQ(*val, 2);
}

void test_real_lru_cache_eviction() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);
    (void)cache.put("c", 3);

    // Inserting a 4th entry should evict "a" (LRU).
    (void)cache.put("d", 4);
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(3));
    ASSERT_TRUE(cache.get("a") == nullptr);
    ASSERT_TRUE(cache.get("d") != nullptr);
    ASSERT_EQ(*cache.get("d"), 4);
}

void test_real_lru_cache_access_promotes() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);
    (void)cache.put("c", 3);

    // Access "a" to promote it to MRU.
    (void)cache.get("a");

    // Insert "d" — should evict "b" (now LRU), not "a".
    (void)cache.put("d", 4);
    ASSERT_TRUE(cache.get("a") != nullptr);
    ASSERT_TRUE(cache.get("b") == nullptr);
    ASSERT_TRUE(cache.get("c") != nullptr);
    ASSERT_TRUE(cache.get("d") != nullptr);
}

void test_real_lru_cache_update_existing() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);
    (void)cache.put("c", 3);

    // Update "a" — should not increase size, and "a" becomes MRU.
    (void)cache.put("a", 100);
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(*cache.get("a"), 100);

    // Insert "d" — "b" should be evicted (LRU after "a" was promoted).
    (void)cache.put("d", 4);
    ASSERT_TRUE(cache.get("b") == nullptr);
    ASSERT_TRUE(cache.get("a") != nullptr);
}

void test_real_lru_cache_erase() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);

    ASSERT_TRUE(cache.erase("a"));
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(cache.get("a") == nullptr);
    ASSERT_FALSE(cache.erase("nonexistent"));
}

void test_real_lru_cache_clear() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    (void)cache.put("b", 2);
    cache.clear();
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(0));
    ASSERT_TRUE(cache.get("a") == nullptr);
}

void test_real_lru_cache_contains() {
    luma::LruCache<std::string, int> cache(3);
    (void)cache.put("a", 1);
    ASSERT_TRUE(cache.contains("a"));
    ASSERT_FALSE(cache.contains("b"));
}

// ─── DAP error message catalogue ───────────────────────────────────

void test_unknown_thread_id_message_format() {
    // The "Unknown thread ID" wording is centralised in one helper so every
    // execution-control site (resume/step in DebugExecutionEngine, snapshot
    // restore in DebugSession) stays in sync. Guard the exact text and the
    // embedded id so a wording drift is caught at one place.
    ASSERT_EQ(luma::dap::error_messages::unknown_thread_id(42),
              std::string("Unknown thread ID 42"));
    ASSERT_EQ(luma::dap::error_messages::unknown_thread_id(0), std::string("Unknown thread ID 0"));
    ASSERT_EQ(luma::dap::error_messages::thread_not_paused(3),
              std::string("Thread 3 is not paused"));
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Miscellaneous Tests");

    // Working directory error reporting.
    RUN(test_working_dir_error_detected);
    RUN(test_working_dir_valid_no_error);

    // Value formatting, logpoint templates, output buffering, capabilities.
    RUN(test_format_value_hex);
    RUN(test_parse_log_template);
    RUN(test_line_buffer_flushes_partial_line_on_sync);
    RUN(test_capabilities_match_implemented_features);

    // DAP error message catalogue.
    RUN(test_unknown_thread_id_message_format);

    // Expression cache LRU eviction.
    RUN(test_lru_cache_max_size_enforcement);
    RUN(test_lru_cache_eviction_order);
    RUN(test_lru_cache_access_refreshes_position);
    RUN(test_lru_cache_duplicate_insert_refreshes);
    RUN(test_lru_cache_clear);
    RUN(test_lru_cache_single_element);

    // Real LruCache template (core/common/lru_cache.hpp).
    RUN(test_real_lru_cache_put_and_get);
    RUN(test_real_lru_cache_eviction);
    RUN(test_real_lru_cache_access_promotes);
    RUN(test_real_lru_cache_update_existing);
    RUN(test_real_lru_cache_erase);
    RUN(test_real_lru_cache_clear);
    RUN(test_real_lru_cache_contains);

    return SUMMARY();
}
