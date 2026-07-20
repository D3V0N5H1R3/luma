// Unit tests for the shared protocol layer (shared/protocol/).

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "parse_error.hpp"
#include "protocol/buffered_transport.hpp"
#include "protocol/constants.hpp"
#include "protocol/error_codes.hpp"
#include "protocol/error_recovery.hpp"
#include "protocol/handler_registry.hpp"
#include "protocol/message_frame.hpp"
#include "protocol/position_utils.hpp"
#include "protocol/transport.hpp"
#include "protocol/transport_exceptions.hpp"
#include "protocol/uri_utils.hpp"
#include "test_framework.hpp"

using namespace luma::protocol;
using luma::json::JsonValue;

// ═══════════════════════════════════════════════════════════
// error_codes.hpp — verify constants have expected values
// ═══════════════════════════════════════════════════════════

static void test_error_code_parse_error() {
    ASSERT_EQ(k_json_rpc_parse_error, -32700);
}

static void test_error_code_invalid_request() {
    ASSERT_EQ(k_json_rpc_invalid_request, -32600);
}

static void test_error_code_method_not_found() {
    ASSERT_EQ(k_json_rpc_method_not_found, -32601);
}

static void test_error_code_invalid_params() {
    ASSERT_EQ(k_json_rpc_invalid_params, -32602);
}

static void test_error_code_internal_error() {
    ASSERT_EQ(k_json_rpc_internal_error, -32603);
}

static void test_error_code_server_not_initialized() {
    ASSERT_EQ(k_json_rpc_server_not_initialized, -32002);
}

static void test_error_code_request_cancelled() {
    ASSERT_EQ(k_json_rpc_request_cancelled, -32800);
}

static void test_error_codes_are_all_negative() {
    ASSERT_LT(k_json_rpc_parse_error, 0);
    ASSERT_LT(k_json_rpc_invalid_request, 0);
    ASSERT_LT(k_json_rpc_method_not_found, 0);
    ASSERT_LT(k_json_rpc_invalid_params, 0);
    ASSERT_LT(k_json_rpc_internal_error, 0);
    ASSERT_LT(k_json_rpc_server_not_initialized, 0);
    ASSERT_LT(k_json_rpc_request_cancelled, 0);
}

static void test_error_codes_no_duplicates() {
    std::set<int> codes{
        k_json_rpc_parse_error,       k_json_rpc_invalid_request, k_json_rpc_method_not_found,
        k_json_rpc_invalid_params,    k_json_rpc_internal_error,  k_json_rpc_server_not_initialized,
        k_json_rpc_request_cancelled,
    };
    ASSERT_EQ(codes.size(), 7u);
}

// ═══════════════════════════════════════════════════════════
// constants.hpp — verify protocol-layer constants
// ═══════════════════════════════════════════════════════════

static void test_constants_max_message_bytes() {
    ASSERT_EQ(k_default_max_message_bytes, std::size_t{50} * 1024 * 1024);
}

static void test_constants_max_header_length() {
    ASSERT_EQ(k_default_max_header_length, 8192u);
}

static void test_constants_max_resync_iterations() {
    ASSERT_EQ(k_default_max_resync_iterations, 1000u);
}

static void test_constants_read_buffer_size() {
    ASSERT_EQ(k_read_buffer_size, 8192u);
}

// ═══════════════════════════════════════════════════════════
// handler_registry.hpp — register, find, unknown method
// ═══════════════════════════════════════════════════════════

static void test_registry_empty() {
    HandlerRegistry<std::function<int()>> registry;
    ASSERT_EQ(registry.size(), 0u);
}

static void test_registry_register_and_find() {
    HandlerRegistry<std::function<int()>> registry;
    registry.register_handler("test/method", []() { return 42; });
    ASSERT_EQ(registry.size(), 1u);

    const auto* handler = registry.find("test/method");
    ASSERT_TRUE(handler != nullptr);
    ASSERT_EQ((*handler)(), 42);
}

static void test_registry_find_unknown_returns_null() {
    HandlerRegistry<std::function<int()>> registry;
    registry.register_handler("known", []() { return 1; });

    const auto* handler = registry.find("unknown");
    ASSERT_TRUE(handler == nullptr);
}

static void test_registry_multiple_handlers() {
    HandlerRegistry<std::function<std::string()>> registry;
    registry.register_handler("a", []() { return std::string{"alpha"}; });
    registry.register_handler("b", []() { return std::string{"beta"}; });
    registry.register_handler("c", []() { return std::string{"gamma"}; });

    ASSERT_EQ(registry.size(), 3u);

    const auto* a = registry.find("a");
    const auto* b = registry.find("b");
    const auto* c = registry.find("c");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ((*a)(), std::string{"alpha"});
    ASSERT_EQ((*b)(), std::string{"beta"});
    ASSERT_EQ((*c)(), std::string{"gamma"});
}

static void test_registry_duplicate_throws() {
    HandlerRegistry<std::function<int()>> registry;
    registry.register_handler("method", []() { return 1; });

    ASSERT_THROWS(registry.register_handler("method", []() { return 2; }));

    ASSERT_EQ(registry.size(), 1u);
}

static void test_registry_with_json_handler() {
    using Handler = std::function<JsonValue(const JsonValue&)>;
    HandlerRegistry<Handler> registry;

    registry.register_handler("echo", [](const JsonValue& params) -> JsonValue { return params; });

    const auto* handler = registry.find("echo");
    ASSERT_TRUE(handler != nullptr);

    auto input = JsonValue::parse(R"({"key": "value"})");
    auto result = (*handler)(input);
    ASSERT_TRUE(result.is_object());
    ASSERT_EQ(result["key"].as_string(), std::string{"value"});
}

static void test_registry_with_void_handler() {
    int call_count = 0;
    using Handler = std::function<void(const std::string&)>;
    HandlerRegistry<Handler> registry;

    registry.register_handler("notify", [&call_count](const std::string&) { ++call_count; });

    const auto* handler = registry.find("notify");
    ASSERT_TRUE(handler != nullptr);
    (*handler)("test");
    ASSERT_EQ(call_count, 1);
}

// ═══════════════════════════════════════════════════════════
// message_frame.hpp — Content-Length parsing and message framing
// ═══════════════════════════════════════════════════════════

static void test_parse_content_length_valid() {
    auto result = try_parse_content_length("Content-Length: 42");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42u);
}

static void test_parse_content_length_zero() {
    auto result = try_parse_content_length("Content-Length: 0");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 0u);
}

static void test_parse_content_length_large() {
    auto result = try_parse_content_length("Content-Length: 1048576");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 1048576u);
}

static void test_parse_content_length_case_insensitive() {
    auto result = try_parse_content_length("content-length: 100");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 100u);
}

static void test_parse_content_length_not_content_length() {
    auto result = try_parse_content_length("Content-Type: application/json");
    ASSERT_FALSE(result.has_value());
}

static void test_parse_content_length_empty_value() {
    auto result = try_parse_content_length("Content-Length: ");
    ASSERT_FALSE(result.has_value());
}

static void test_parse_content_length_non_numeric() {
    auto result = try_parse_content_length("Content-Length: abc");
    ASSERT_FALSE(result.has_value());
}

static void test_parse_content_length_negative() {
    auto result = try_parse_content_length("Content-Length: -1");
    ASSERT_FALSE(result.has_value());
}

static void test_parse_content_length_unrelated_header() {
    auto result = try_parse_content_length("X-Custom-Header: 999");
    ASSERT_FALSE(result.has_value());
}

static void test_parse_content_length_empty_string() {
    auto result = try_parse_content_length("");
    ASSERT_FALSE(result.has_value());
}

static void test_format_protocol_message() {
    JsonValue::ObjectType obj;
    obj["jsonrpc"] = JsonValue{std::string{"2.0"}};
    obj["method"] = JsonValue{std::string{"test"}};
    JsonValue msg{std::move(obj)};

    auto formatted = write_framed_message(msg.to_string());

    // Should start with Content-Length header.
    ASSERT_TRUE(formatted.find("Content-Length: ") == 0);

    // Should contain the \r\n\r\n separator.
    auto sep_pos = formatted.find("\r\n\r\n");
    ASSERT_TRUE(sep_pos != std::string::npos);

    // Body after separator should be valid JSON.
    auto body = formatted.substr(sep_pos + 4);
    auto parsed = JsonValue::parse(body);
    ASSERT_TRUE(parsed.is_object());
    ASSERT_EQ(parsed["method"].as_string(), std::string{"test"});
}

static void test_format_protocol_message_content_length_matches_body() {
    JsonValue::ObjectType obj;
    obj["id"] = JsonValue{static_cast<int64_t>(1)};
    JsonValue msg{std::move(obj)};

    auto formatted = write_framed_message(msg.to_string());

    // Extract the Content-Length value.
    auto colon_pos = formatted.find(": ");
    auto crlf_pos = formatted.find("\r\n");
    auto length_str = formatted.substr(colon_pos + 2, crlf_pos - colon_pos - 2);
    auto expected_length = std::stoull(length_str);

    // Extract the body.
    auto sep_pos = formatted.find("\r\n\r\n");
    auto body = formatted.substr(sep_pos + 4);

    ASSERT_EQ(body.size(), expected_length);
}

// ═══════════════════════════════════════════════════════════
// MockTransport — testable subclass for Transport base class
// ═══════════════════════════════════════════════════════════

class MockTransport : public Transport {
public:
    using Transport::Transport;

    // Queue lines for read_line() to return.
    void queue_line(std::string line) {
        lines_.push_back(std::move(line));
    }

    // Queue raw data for read_exact() to return.
    void queue_exact(std::string data) {
        exact_data_.push_back(std::move(data));
    }

    // Access written messages.
    [[nodiscard]] const std::vector<JsonValue>& written() const {
        return written_;
    }

    void write_message(const JsonValue& message) override {
        written_.push_back(message);
    }

protected:
    [[nodiscard]] std::optional<std::string> read_line() override {
        if (line_index_ >= lines_.size()) {
            return std::nullopt;
        }
        return lines_[line_index_++];
    }

    [[nodiscard]] std::string read_exact(std::size_t /*count*/) override {
        if (exact_index_ >= exact_data_.size()) {
            return {};
        }
        return exact_data_[exact_index_++];
    }

private:
    std::vector<std::string> lines_;
    std::size_t line_index_{0};
    std::vector<std::string> exact_data_;
    std::size_t exact_index_{0};
    std::vector<JsonValue> written_;
};

static void test_transport_read_message_basic() {
    MockTransport transport;
    transport.queue_line("Content-Length: 14");
    transport.queue_line(""); // blank line ends headers
    transport.queue_exact(R"({"id":"test"})");

    auto msg = transport.read_message();
    ASSERT_TRUE(msg.has_value());
    ASSERT_TRUE(msg->is_object());
    ASSERT_EQ((*msg)["id"].as_string(), std::string{"test"});
}

static void test_transport_read_message_eof() {
    MockTransport transport;
    // No lines queued — immediate EOF.
    auto msg = transport.read_message();
    ASSERT_FALSE(msg.has_value());
}

static void test_transport_read_message_ignores_other_headers() {
    MockTransport transport;
    transport.queue_line("Content-Type: application/json");
    transport.queue_line("Content-Length: 14");
    transport.queue_line(""); // blank line ends headers
    transport.queue_exact(R"({"id":"test"})");

    auto msg = transport.read_message();
    ASSERT_TRUE(msg.has_value());
}

static void test_transport_rejects_oversized_message() {
    // Use a tiny limit to test the size check.
    MockTransport transport(TransportLimits{.max_message_bytes = 10});

    // Suppress error output during test.
    transport.set_error_callback([](std::string_view) {});

    transport.queue_line("Content-Length: 100");
    transport.queue_line(""); // blank line ends headers
    // After the parse error, resync will try to read lines and hit EOF.
    auto msg = transport.read_message();
    ASSERT_FALSE(msg.has_value());
}

static void test_transport_error_callback() {
    MockTransport transport;
    std::string captured_error;
    transport.set_error_callback([&](std::string_view msg) { captured_error = std::string{msg}; });

    // Feed invalid JSON to trigger an error.
    transport.queue_line("Content-Length: 3");
    transport.queue_line("");
    transport.queue_exact("{x}"); // invalid JSON

    auto msg = transport.read_message();
    ASSERT_FALSE(msg.has_value());
    ASSERT_TRUE(!captured_error.empty());
}

// ═══════════════════════════════════════════════════════════
// BufferedMockTransport — exercises BufferedTransport::read_line()
// directly via an in-memory byte stream.  The MockTransport above
// overrides read_line(), so it does not cover the buffered framing
// path or its max_header_length() enforcement.
// ═══════════════════════════════════════════════════════════

class BufferedMockTransport : public BufferedTransport {
public:
    using BufferedTransport::BufferedTransport;

    // Queue raw bytes for read_raw() to hand out.
    void queue_raw(std::string data) {
        data_ += std::move(data);
    }

    void write_message(const JsonValue& /*message*/) override {}

    // Expose the protected read_line() primitive for direct testing.
    [[nodiscard]] std::optional<std::string> call_read_line() {
        return read_line();
    }

protected:
    std::size_t read_raw(std::span<char> buf) override {
        const auto remaining = data_.size() - pos_;
        const auto count = std::min(buf.size(), remaining);
        std::copy_n(data_.data() + pos_, count, buf.data());
        pos_ += count;
        return count;
    }

private:
    std::string data_;
    std::size_t pos_{0};
};

static void test_buffered_transport_read_line_strips_crlf() {
    BufferedMockTransport transport;
    transport.queue_raw("Content-Length: 42\r\n");

    const auto line = transport.call_read_line();
    ASSERT_TRUE(line.has_value());
    ASSERT_EQ(*line, std::string{"Content-Length: 42"});
}

static void test_buffered_transport_header_line_too_long_throws() {
    // Tight header-length cap so the overflow path is cheap to trigger.
    BufferedMockTransport transport(TransportLimits{.max_header_length = 16});

    // A terminated header line longer than the cap is rejected (newline branch).
    transport.queue_raw(std::string(64, 'a') + "\n");

    ASSERT_THROWS_WITH_MESSAGE(transport.call_read_line(), "Header line exceeds");
}

static void test_buffered_transport_unterminated_header_too_long_throws() {
    BufferedMockTransport transport(TransportLimits{.max_header_length = 16});

    // No newline at all — the cap is enforced on the buffer-exhausted branch.
    transport.queue_raw(std::string(64, 'a'));

    ASSERT_THROWS_WITH_MESSAGE(transport.call_read_line(), "Header line exceeds");
}

// ═══════════════════════════════════════════════════════════
// parse_error.hpp — exception hierarchy
// ═══════════════════════════════════════════════════════════

static void test_parse_error_base() {
    luma::ParseError err("test error");
    ASSERT_EQ(std::string{err.what()}, std::string{"test error"});
    ASSERT_EQ(err.offset(), -1);
}

static void test_json_parse_error() {
    luma::JsonParseError err(42, "unexpected token");
    ASSERT_EQ(err.position(), 42u);
    ASSERT_TRUE(std::string{err.what()}.find("42") != std::string::npos);
}

static void test_transport_exceptions_hierarchy() {
    // Verify ConnectionClosed is catchable as TransportError.
    bool caught_as_transport = false;
    try {
        throw ConnectionClosed("connection lost");
    } catch (const TransportError&) {
        caught_as_transport = true;
    }
    ASSERT_TRUE(caught_as_transport);

    // Verify protocol::ParseError is catchable as luma::ParseError.
    bool caught_as_parse = false;
    try {
        throw luma::protocol::ParseError("bad header");
    } catch (const luma::ParseError&) {
        caught_as_parse = true;
    }
    ASSERT_TRUE(caught_as_parse);
}

// ═══════════════════════════════════════════════════════════
// uri_utils.hpp — percent codec and URI conversion
// ═══════════════════════════════════════════════════════════

static void test_percent_decode_plain() {
    ASSERT_EQ(percent_decode("hello"), std::string{"hello"});
}

static void test_percent_decode_encoded() {
    ASSERT_EQ(percent_decode("hello%20world"), std::string{"hello world"});
}

static void test_percent_decode_uppercase_hex() {
    ASSERT_EQ(percent_decode("%41"), std::string{"A"});
}

static void test_percent_decode_null_byte_dropped() {
    // %00 must be silently dropped to prevent path validation bypass.
    ASSERT_EQ(percent_decode("a%00b"), std::string{"ab"});
}

static void test_percent_encode_path_plain() {
    ASSERT_EQ(percent_encode_path("/home/user"), std::string{"/home/user"});
}

static void test_percent_encode_path_space() {
    auto result = percent_encode_path("/path/my file");
    ASSERT_TRUE(result.find("%20") != std::string::npos);
}

static void test_percent_decode_encode_roundtrip() {
    const std::string original = "/path/to/file with spaces & symbols!";
    const auto encoded = percent_encode_path(original);
    const auto decoded = percent_decode(encoded);
    // Backslash normalisation means we compare after normalising separators.
    ASSERT_EQ(decoded, original);
}

static void test_uri_to_path_non_file_uri() {
    auto result = uri_to_path("https://example.com/file.txt");
    ASSERT_FALSE(result.has_value());
}

static void test_uri_to_path_empty_string() {
    auto result = uri_to_path("");
    ASSERT_FALSE(result.has_value());
}

static void test_path_to_uri_roundtrip() {
    // Simple ASCII path.
#ifdef _WIN32
    const std::string path = "C:\\Users\\test\\file.txt";
#else
    const std::string path = "/home/test/file.txt";
#endif
    auto uri = path_to_uri(path);
    ASSERT_TRUE(uri.starts_with("file://"));

    auto back = uri_to_path(uri);
    ASSERT_TRUE(back.has_value());
}

static void test_canonicalize_uri_lowercase_drive() {
    auto result = canonicalize_uri("file:///C:/Users/test");
    ASSERT_EQ(result, std::string{"file:///c:/Users/test"});
}

static void test_canonicalize_uri_already_lowercase() {
    auto result = canonicalize_uri("file:///c:/Users/test");
    ASSERT_EQ(result, std::string{"file:///c:/Users/test"});
}

static void test_canonicalize_uri_unix_path() {
    auto result = canonicalize_uri("file:///home/user/file.txt");
    ASSERT_EQ(result, std::string{"file:///home/user/file.txt"});
}

// ═══════════════════════════════════════════════════════════
// position_utils.hpp — UTF-8 ↔ UTF-16 column conversion
//
// Multi-byte sequences are built from explicit byte escapes so the tests do
// not depend on the source file's encoding.  Each escaped literal is closed
// before the next to avoid greedy \x hex parsing swallowing following digits.
// ═══════════════════════════════════════════════════════════

// U+00E9 é  — 2 UTF-8 bytes, 1 UTF-16 code unit.
static const std::string k_e_acute = "\xC3\xA9";
// U+20AC €  — 3 UTF-8 bytes, 1 UTF-16 code unit.
static const std::string k_euro = "\xE2\x82\xAC";
// U+1D11E 𝄞 — 4 UTF-8 bytes, 2 UTF-16 code units (surrogate pair).
static const std::string k_gclef = "\xF0\x9D\x84\x9E";

static void test_is_supplementary_sequence() {
    ASSERT_FALSE(is_supplementary_sequence(1));
    ASSERT_FALSE(is_supplementary_sequence(2));
    ASSERT_FALSE(is_supplementary_sequence(3));
    ASSERT_TRUE(is_supplementary_sequence(4));
}

static void test_byte_offset_to_utf16_ascii() {
    ASSERT_EQ(byte_offset_to_utf16_column("hello", 5), 5);
    ASSERT_EQ(byte_offset_to_utf16_column("hello", 0), 0);
    ASSERT_EQ(byte_offset_to_utf16_column("hello", 3), 3);
}

static void test_byte_offset_to_utf16_two_byte() {
    const std::string line = "a" + k_e_acute + "b"; // a é b — 4 bytes, 3 units
    ASSERT_EQ(byte_offset_to_utf16_column(line, line.size()), 3);
    ASSERT_EQ(byte_offset_to_utf16_column(line, 1), 1); // after 'a'
    ASSERT_EQ(byte_offset_to_utf16_column(line, 3), 2); // after 'a' + é
}

static void test_byte_offset_to_utf16_three_byte() {
    ASSERT_EQ(byte_offset_to_utf16_column(k_euro, k_euro.size()), 1);
}

static void test_byte_offset_to_utf16_surrogate_pair() {
    // A 4-byte sequence occupies two UTF-16 code units.
    ASSERT_EQ(byte_offset_to_utf16_column(k_gclef, k_gclef.size()), 2);
}

static void test_utf16_to_byte_offset_ascii() {
    ASSERT_EQ(utf16_column_to_byte_offset("hello", 5), 5u);
    ASSERT_EQ(utf16_column_to_byte_offset("hello", 0), 0u);
    ASSERT_EQ(utf16_column_to_byte_offset("hello", 3), 3u);
}

static void test_utf16_to_byte_offset_two_byte() {
    const std::string line = "a" + k_e_acute + "b";
    ASSERT_EQ(utf16_column_to_byte_offset(line, 2), 3u); // 'a' + é span 3 bytes
    ASSERT_EQ(utf16_column_to_byte_offset(line, 3), 4u);
}

static void test_utf16_to_byte_offset_surrogate_pair() {
    ASSERT_EQ(utf16_column_to_byte_offset(k_gclef, 2), 4u); // full surrogate pair
    ASSERT_EQ(utf16_column_to_byte_offset(k_gclef, 0), 0u);
    // Column 1 lands inside the surrogate pair; the offset clamps to the end of
    // the 4-byte sequence because a codepoint cannot be split.
    ASSERT_EQ(utf16_column_to_byte_offset(k_gclef, 1), 4u);
}

static void test_utf16_byte_offset_roundtrip_mixed() {
    // a € 𝄞 b — bytes: 1 + 3 + 4 + 1 = 9; units: 1 + 1 + 2 + 1 = 5.
    const std::string line = "a" + k_euro + k_gclef + "b";
    ASSERT_EQ(line.size(), 9u);
    ASSERT_EQ(byte_offset_to_utf16_column(line, line.size()), 5);
    ASSERT_EQ(utf16_column_to_byte_offset(line, 5), 9u);
    // Checkpoints at codepoint boundaries round-trip cleanly.
    ASSERT_EQ(byte_offset_to_utf16_column(line, 4), 2); // after a + €
    ASSERT_EQ(utf16_column_to_byte_offset(line, 2), 4u);
    ASSERT_EQ(byte_offset_to_utf16_column(line, 8), 4); // after a + € + 𝄞
    ASSERT_EQ(utf16_column_to_byte_offset(line, 4), 8u);
}

// ═══════════════════════════════════════════════════════════
// error_recovery.hpp — severity classification and recovery state
// ═══════════════════════════════════════════════════════════

static void test_classify_connection_closed_is_fatal() {
    ConnectionClosed e{"pipe gone"};
    ASSERT_EQ(classify_read_error(e), ErrorSeverity::fatal);
}

static void test_classify_parse_error_is_transient() {
    ParseError e{"bad header"};
    ASSERT_EQ(classify_read_error(e), ErrorSeverity::transient);
}

static void test_classify_transport_error_is_fatal() {
    TransportError e{"io failure"};
    ASSERT_EQ(classify_read_error(e), ErrorSeverity::fatal);
}

static void test_classify_unknown_exception_is_transient() {
    std::runtime_error e{"something else"};
    ASSERT_EQ(classify_read_error(e), ErrorSeverity::transient);
}

static void test_recovery_state_fatal_shuts_down() {
    ErrorRecoveryState recovery;
    ASSERT_EQ(recovery.on_error(ErrorSeverity::fatal), RecoveryAction::shutdown);
    // A fatal error does not touch the transient counter.
    ASSERT_EQ(recovery.consecutive_errors(), 0u);
}

static void test_recovery_state_transient_continues() {
    ErrorRecoveryState recovery{3};
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
    ASSERT_EQ(recovery.consecutive_errors(), 1u);
}

static void test_recovery_state_threshold_shuts_down() {
    ErrorRecoveryState recovery{3};
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
    // The third consecutive transient error reaches the threshold.
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::shutdown);
    ASSERT_EQ(recovery.consecutive_errors(), 3u);
}

static void test_recovery_state_success_resets_counter() {
    ErrorRecoveryState recovery{3};
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
    ASSERT_EQ(recovery.consecutive_errors(), 2u);
    recovery.on_success();
    ASSERT_EQ(recovery.consecutive_errors(), 0u);
    // After a reset the counter climbs from zero again.
    ASSERT_EQ(recovery.on_error(ErrorSeverity::transient), RecoveryAction::skip_and_continue);
}

static void test_recovery_state_default_threshold() {
    ErrorRecoveryState recovery;
    ASSERT_EQ(recovery.max_consecutive_errors(), k_default_max_consecutive_errors);
}

// ═══════════════════════════════════════════════════════════
// Transport::resync_to_next_message — recovery after a parse error
// (exercised through read_message via the MockTransport defined above)
// ═══════════════════════════════════════════════════════════

static void test_transport_resync_recovers_to_next_message() {
    MockTransport transport;
    transport.set_error_callback([](std::string_view) {}); // suppress error output

    // First message: valid header, invalid JSON body → transient parse error.
    transport.queue_line("Content-Length: 3");
    transport.queue_line("");
    transport.queue_exact("{x}");

    // Resync scans forward and buffers the next Content-Length header.
    transport.queue_line("Content-Length: 11");
    // The second message's headers continue after the buffered header line.
    transport.queue_line("");
    transport.queue_exact(R"({"id":"ok"})");

    // First read reports the error and returns nullopt after resyncing.
    auto first = transport.read_message();
    ASSERT_FALSE(first.has_value());

    // Second read succeeds using the header buffered during resync.
    auto second = transport.read_message();
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ((*second)["id"].as_string(), std::string{"ok"});
}

static void test_transport_resync_cap_throws() {
    // Tight cap so exhausting resync is cheap to trigger.
    MockTransport transport(TransportLimits{.max_resync_iterations = 3});
    transport.set_error_callback([](std::string_view) {}); // suppress error output

    // Valid header, invalid body → parse error starts the resync scan.
    transport.queue_line("Content-Length: 3");
    transport.queue_line("");
    transport.queue_exact("{x}");

    // Only non-Content-Length lines follow, so resync never finds a boundary
    // and exhausts its iteration cap.
    transport.queue_line("junk-a");
    transport.queue_line("junk-b");
    transport.queue_line("junk-c");

    // Resync failure is unrecoverable and propagates out of read_message().
    ASSERT_THROWS_WITH_MESSAGE(transport.read_message(), "Resync failed");
}

// ═══════════════════════════════════════════════════════════

int main() {
    // Error codes.
    RUN(test_error_code_parse_error);
    RUN(test_error_code_invalid_request);
    RUN(test_error_code_method_not_found);
    RUN(test_error_code_invalid_params);
    RUN(test_error_code_internal_error);
    RUN(test_error_code_server_not_initialized);
    RUN(test_error_code_request_cancelled);
    RUN(test_error_codes_are_all_negative);
    RUN(test_error_codes_no_duplicates);

    // Constants.
    RUN(test_constants_max_message_bytes);
    RUN(test_constants_max_header_length);
    RUN(test_constants_max_resync_iterations);
    RUN(test_constants_read_buffer_size);

    // Handler registry.
    RUN(test_registry_empty);
    RUN(test_registry_register_and_find);
    RUN(test_registry_find_unknown_returns_null);
    RUN(test_registry_multiple_handlers);
    RUN(test_registry_duplicate_throws);
    RUN(test_registry_with_json_handler);
    RUN(test_registry_with_void_handler);

    // Transport framing.
    RUN(test_parse_content_length_valid);
    RUN(test_parse_content_length_zero);
    RUN(test_parse_content_length_large);
    RUN(test_parse_content_length_case_insensitive);
    RUN(test_parse_content_length_not_content_length);
    RUN(test_parse_content_length_empty_value);
    RUN(test_parse_content_length_non_numeric);
    RUN(test_parse_content_length_negative);
    RUN(test_parse_content_length_unrelated_header);
    RUN(test_parse_content_length_empty_string);
    RUN(test_format_protocol_message);
    RUN(test_format_protocol_message_content_length_matches_body);

    // Transport read/write via mock.
    RUN(test_transport_read_message_basic);
    RUN(test_transport_read_message_eof);
    RUN(test_transport_read_message_ignores_other_headers);
    RUN(test_transport_rejects_oversized_message);
    RUN(test_transport_error_callback);

    // BufferedTransport read_line() framing and header-length enforcement.
    RUN(test_buffered_transport_read_line_strips_crlf);
    RUN(test_buffered_transport_header_line_too_long_throws);
    RUN(test_buffered_transport_unterminated_header_too_long_throws);

    // Exception hierarchy.
    RUN(test_parse_error_base);
    RUN(test_json_parse_error);
    RUN(test_transport_exceptions_hierarchy);

    // URI utilities — percent codec.
    RUN(test_percent_decode_plain);
    RUN(test_percent_decode_encoded);
    RUN(test_percent_decode_uppercase_hex);
    RUN(test_percent_decode_null_byte_dropped);
    RUN(test_percent_encode_path_plain);
    RUN(test_percent_encode_path_space);
    RUN(test_percent_decode_encode_roundtrip);

    // URI utilities — conversion.
    RUN(test_uri_to_path_non_file_uri);
    RUN(test_uri_to_path_empty_string);
    RUN(test_path_to_uri_roundtrip);
    RUN(test_canonicalize_uri_lowercase_drive);
    RUN(test_canonicalize_uri_already_lowercase);
    RUN(test_canonicalize_uri_unix_path);

    // Position utilities — UTF-8 ↔ UTF-16 column conversion.
    RUN(test_is_supplementary_sequence);
    RUN(test_byte_offset_to_utf16_ascii);
    RUN(test_byte_offset_to_utf16_two_byte);
    RUN(test_byte_offset_to_utf16_three_byte);
    RUN(test_byte_offset_to_utf16_surrogate_pair);
    RUN(test_utf16_to_byte_offset_ascii);
    RUN(test_utf16_to_byte_offset_two_byte);
    RUN(test_utf16_to_byte_offset_surrogate_pair);
    RUN(test_utf16_byte_offset_roundtrip_mixed);

    // Error recovery — severity classification and consecutive-error tracking.
    RUN(test_classify_connection_closed_is_fatal);
    RUN(test_classify_parse_error_is_transient);
    RUN(test_classify_transport_error_is_fatal);
    RUN(test_classify_unknown_exception_is_transient);
    RUN(test_recovery_state_fatal_shuts_down);
    RUN(test_recovery_state_transient_continues);
    RUN(test_recovery_state_threshold_shuts_down);
    RUN(test_recovery_state_success_resets_counter);
    RUN(test_recovery_state_default_threshold);

    // Transport resync recovery.
    RUN(test_transport_resync_recovers_to_next_message);
    RUN(test_transport_resync_cap_throws);

    return SUMMARY();
}
