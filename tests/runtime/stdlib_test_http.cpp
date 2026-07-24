// Standard library tests: Http.

#include <string>

#include "runtime/stdlib/io/http_module_response.hpp"
#include "stdlib_test_helpers.hpp"

static void test_http_build_query() {
    const auto v = eval(R"(Http.build_query({"key": "val"}))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "key=val");
}

// HTTPS request to a loopback literal IP. The SSRF guard rejects loopback
// addresses BEFORE any socket I/O, so this is deterministic and needs no
// network. The assertions verify that HTTPS is actually compiled into this
// build: with TLS enabled the request must reach the SSRF guard rather than the
// up-front "requires TLS" stub. This guards against the dead-TLS regression
// where the luma_stdlib_io object library is built without LUMA_HAS_TLS, which
// silently turns every https:// request into a "requires TLS" failure even
// though luma_core itself was linked against Mbed TLS.
static void test_http_https_reaches_request_pipeline() {
    const auto v = eval(R"(Http.get("https://127.0.0.1/admin"))");

    ASSERT_RESULT_FAILURE(v);

    const auto message = v.as_result()->owned_inner->as_string();

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
    ASSERT_TRUE(message.find("requires TLS") == std::string::npos);
    ASSERT_TRUE(message.find("blocked") != std::string::npos);
#else
    ASSERT_TRUE(message.find("requires TLS") != std::string::npos);
#endif
}

static void test_http_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Http.get"));
    ASSERT_TRUE(env->has("Http.post"));
    ASSERT_TRUE(env->has("Http.put"));
    ASSERT_TRUE(env->has("Http.delete"));
    ASSERT_TRUE(env->has("Http.patch"));
    ASSERT_TRUE(env->has("Http.head"));
    ASSERT_TRUE(env->has("Http.get_with"));
    ASSERT_TRUE(env->has("Http.post_with"));
    ASSERT_TRUE(env->has("Http.request"));
    ASSERT_TRUE(env->has("Http.parse_url"));
    ASSERT_TRUE(env->has("Http.build_query"));
    ASSERT_TRUE(env->has("Http.parse_query"));
    ASSERT_TRUE(env->has("Http.download"));
    ASSERT_TRUE(env->has("Http.basic_auth"));
    ASSERT_TRUE(env->has("Http.bearer_auth"));
    ASSERT_TRUE(env->has("Http.delete_with"));
    ASSERT_TRUE(env->has("Http.patch_with"));
    ASSERT_TRUE(env->has("Http.put_with"));
    ASSERT_TRUE(env->has("Http.method_to_string"));
}

static void test_http_parse_query() {
    const auto v = eval(R"(Http.parse_query("a=1&b=2"))");

    ASSERT_TRUE(v.is_dictionary());

    const auto* a = v.as_dictionary()->find("a");

    ASSERT_TRUE(a && a->is_string());
    ASSERT_EQ(a->as_string(), "1");

    const auto* b = v.as_dictionary()->find("b");

    ASSERT_TRUE(b && b->is_string());
    ASSERT_EQ(b->as_string(), "2");
}

static void test_http_parse_url() {
    const auto v = eval(R"(Http.parse_url("http://example.com:8080/path?q=1"))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, "UrlParts");

    const auto* host = v.as_record()->find_field("host");

    ASSERT_TRUE(host && host->is_string());

    ASSERT_EQ(host->as_string(), "example.com");

    const auto* port = v.as_record()->find_field("port");

    ASSERT_TRUE(port && port->is_string());
    ASSERT_EQ(port->as_string(), "8080");

    const auto* path = v.as_record()->find_field("path");

    ASSERT_TRUE(path && path->is_string());
    ASSERT_EQ(path->as_string(), "/path");

    const auto* query = v.as_record()->find_field("query");

    ASSERT_TRUE(query && query->is_string());
    ASSERT_EQ(query->as_string(), "q=1");
}

static void test_http_basic_auth() {
    // Base64("user:pass") == "dXNlcjpwYXNz"
    const auto v = eval(R"(Http.basic_auth("user", "pass"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Basic dXNlcjpwYXNz");
}

static void test_http_basic_auth_empty_credentials() {
    // Encodes ":" (separator only) → Base64(":") = "Og=="
    const auto v = eval(R"(Http.basic_auth("", ""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Basic Og==");
}

static void test_http_bearer_auth() {
    const auto v = eval(R"(Http.bearer_auth("my-token-123"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Bearer my-token-123");
}

static void test_http_bearer_auth_empty() {
    const auto v = eval(R"(Http.bearer_auth(""))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Bearer ");
}

// ─── Http: method_to_string (Http.Method choice → verb string) ────────────────

static void test_http_method_to_string_all_verbs() {
    // Each Http.Method variant converts to its uppercase HTTP verb.
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Get)").as_string(), "GET");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Post)").as_string(), "POST");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Put)").as_string(), "PUT");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Patch)").as_string(), "PATCH");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Delete)").as_string(), "DELETE");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Head)").as_string(), "HEAD");
    ASSERT_EQ(eval("Http.method_to_string(Http.Method.Options)").as_string(), "OPTIONS");
}

static void test_http_method_to_string_rejects_non_choice() {
    // A plain string is not an Http.Method choice — the converter rejects it.
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.method_to_string("GET"))"));
}

// ─── Http: status_class (raw status → Http.StatusClass family) ────────────────

static void test_http_status_class_all_families() {
    // Each family boundary classifies into the right RFC 9110 Http.StatusClass.
    const auto info = eval("Http.status_class(100)");
    ASSERT_RESULT_SUCCESS(info);
    ASSERT_TRUE(info.as_result()->owned_inner->is_choice());
    ASSERT_EQ(info.as_result()->owned_inner->as_choice()->type_name, "StatusClass");
    ASSERT_EQ(info.as_result()->owned_inner->as_choice()->variant, "Informational");

    ASSERT_EQ(eval("Http.status_class(200)").as_result()->owned_inner->as_choice()->variant,
              "Success");
    ASSERT_EQ(eval("Http.status_class(301)").as_result()->owned_inner->as_choice()->variant,
              "Redirection");
    ASSERT_EQ(eval("Http.status_class(404)").as_result()->owned_inner->as_choice()->variant,
              "ClientError");
    ASSERT_EQ(eval("Http.status_class(503)").as_result()->owned_inner->as_choice()->variant,
              "ServerError");
}

static void test_http_status_class_out_of_range_fails() {
    // A code outside 100-599 has no family, so status_class returns a failure.
    ASSERT_EVAL_FAILURE("Http.status_class(99)");
    ASSERT_EVAL_FAILURE("Http.status_class(600)");
    ASSERT_EVAL_FAILURE("Http.status_class(0)");
}

// ─── Http: is_success (2xx predicate over a response record) ──────────────────

static void test_http_is_success_reads_status() {
    // is_success is true only for a 2xx status.  A response record is built here
    // with a literal (the field the predicate reads is `status`); eval() ignores
    // the "record type not found at compile time" warning stdlib records emit.
    ASSERT_EQ(
        eval(
            R"(Http.is_success(Http.Response { status = 204, reason = "x", body = "", headers = {} }))")
            .as_bool(),
        true);
    ASSERT_EQ(
        eval(
            R"(Http.is_success(Http.Response { status = 200, reason = "x", body = "", headers = {} }))")
            .as_bool(),
        true);
    ASSERT_EQ(
        eval(
            R"(Http.is_success(Http.Response { status = 404, reason = "x", body = "", headers = {} }))")
            .as_bool(),
        false);
    ASSERT_EQ(
        eval(
            R"(Http.is_success(Http.Response { status = 500, reason = "x", body = "", headers = {} }))")
            .as_bool(),
        false);
    ASSERT_EQ(
        eval(
            R"(Http.is_success(Http.Response { status = 100, reason = "x", body = "", headers = {} }))")
            .as_bool(),
        false);
}

static void test_http_new_functions_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Http.basic_auth"));
    ASSERT_TRUE(env->has("Http.bearer_auth"));
    ASSERT_TRUE(env->has("Http.delete_with"));
    ASSERT_TRUE(env->has("Http.patch_with"));
    ASSERT_TRUE(env->has("Http.put_with"));
    ASSERT_TRUE(env->has("Http.status_class"));
    ASSERT_TRUE(env->has("Http.is_success"));
}

// ─── Http: URL parsing (positive) ────────────────────────────────────────────

static void test_http_parse_url_default_http_port() {
    // No explicit port on an http URL defaults to 80.
    const auto v = eval(R"(Http.parse_url("http://example.com/page"))");

    ASSERT_TRUE(v.is_record());

    const auto* port = v.as_record()->find_field("port");

    ASSERT_TRUE(port && port->is_string());
    ASSERT_EQ(port->as_string(), "80");
}

static void test_http_parse_url_https_default_port() {
    // https URLs default to port 443 and lowercase the scheme.
    const auto v = eval(R"(Http.parse_url("https://example.com/page"))");

    ASSERT_TRUE(v.is_record());

    const auto* scheme = v.as_record()->find_field("scheme");
    const auto* port = v.as_record()->find_field("port");

    ASSERT_TRUE(scheme && scheme->is_string());
    ASSERT_EQ(scheme->as_string(), "https");
    ASSERT_TRUE(port && port->is_string());
    ASSERT_EQ(port->as_string(), "443");
}

static void test_http_parse_url_ipv6() {
    // Bracketed IPv6 authority keeps the brackets in the host and parses the
    // trailing port, path, and query.
    const auto v = eval(R"(Http.parse_url("http://[2001:db8::1]:8080/p?q=1"))");

    ASSERT_TRUE(v.is_record());

    const auto* host = v.as_record()->find_field("host");
    const auto* port = v.as_record()->find_field("port");
    const auto* path = v.as_record()->find_field("path");
    const auto* query = v.as_record()->find_field("query");

    ASSERT_TRUE(host && host->is_string());
    ASSERT_EQ(host->as_string(), "[2001:db8::1]");
    ASSERT_TRUE(port && port->is_string());
    ASSERT_EQ(port->as_string(), "8080");
    ASSERT_TRUE(path && path->is_string());
    ASSERT_EQ(path->as_string(), "/p");
    ASSERT_TRUE(query && query->is_string());
    ASSERT_EQ(query->as_string(), "q=1");
}

static void test_http_parse_url_scheme_lowercased() {
    // The scheme is normalised to lowercase; the host case is preserved.
    const auto v = eval(R"(Http.parse_url("HTTP://Example.COM/Path"))");

    ASSERT_TRUE(v.is_record());

    const auto* scheme = v.as_record()->find_field("scheme");
    const auto* host = v.as_record()->find_field("host");

    ASSERT_TRUE(scheme && scheme->is_string());
    ASSERT_EQ(scheme->as_string(), "http");
    ASSERT_TRUE(host && host->is_string());
    ASSERT_EQ(host->as_string(), "Example.COM");
}

static void test_http_parse_url_no_path() {
    // A URL with no path component yields the root path "/".
    const auto v = eval(R"(Http.parse_url("http://example.com"))");

    ASSERT_TRUE(v.is_record());

    const auto* path = v.as_record()->find_field("path");

    ASSERT_TRUE(path && path->is_string());
    ASSERT_EQ(path->as_string(), "/");
}

// ─── Http: query string (positive) ───────────────────────────────────────────

static void test_http_build_query_encodes() {
    // Space and '&' in a value must be percent-encoded.
    const auto v = eval(R"(Http.build_query({"q": "a b&c"}))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "q=a%20b%26c");
}

static void test_http_build_query_empty() {
    // An empty dictionary builds an empty query string.
    const auto v = eval(R"(Http.build_query({}))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "");
}

static void test_http_parse_query_decodes_and_empty_value() {
    // '%20' decodes to a space and a key with no '=' yields an empty value.
    const auto v = eval(R"(Http.parse_query("name=a%20b&flag&x=1"))");

    ASSERT_TRUE(v.is_dictionary());

    const auto* name = v.as_dictionary()->find("name");
    const auto* flag = v.as_dictionary()->find("flag");
    const auto* x = v.as_dictionary()->find("x");

    ASSERT_TRUE(name && name->is_string());
    ASSERT_EQ(name->as_string(), "a b");
    ASSERT_TRUE(flag && flag->is_string());
    ASSERT_EQ(flag->as_string(), "");
    ASSERT_TRUE(x && x->is_string());
    ASSERT_EQ(x->as_string(), "1");
}

static void test_http_query_roundtrip_reserved_chars() {
    // build_query → parse_query restores reserved characters exactly.
    const auto v =
        eval(R"(Http.parse_query(Http.build_query({"name": "a&b", "v": "x y"}))["name"])");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "a&b");
}

// ─── Http: argument type validation (negative — throws) ───────────────────────

static void test_http_parse_url_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws("Http.parse_url(123)"));
}

static void test_http_parse_query_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws("Http.parse_query(123)"));
}

static void test_http_build_query_rejects_non_dict() {
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.build_query("not-a-dict"))"));
}

static void test_http_get_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws("Http.get(123)"));
}

static void test_http_basic_auth_rejects_non_string() {
    ASSERT_TRUE(luma::test::eval_throws("Http.basic_auth(1, 2)"));
}

static void test_http_get_with_rejects_non_string_header_value() {
    // Every HTTP header value must be a string; a non-string value is rejected.
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.get_with("http://example.com", {"X": 1}))"));
}

// ─── Http: request validation (negative — failure result, no network) ─────────

static void test_http_unsupported_scheme_fails() {
    // Schemes other than http/https are rejected before any network activity.
    ASSERT_EVAL_FAILURE(R"(Http.get("ftp://example.com/file"))");
}

static void test_http_empty_host_fails() {
    ASSERT_EVAL_FAILURE(R"(Http.get("http://"))");
}

static void test_http_crlf_path_injection_fails() {
    // CR/LF in the request target is request-line injection and must be rejected.
    ASSERT_EVAL_FAILURE(R"(Http.get("http://example.com/a\r\nX-Injected: 1"))");
}

static void test_http_crlf_header_injection_fails() {
    // CR/LF in a header value is header injection and must be rejected.
    ASSERT_EVAL_FAILURE(R"(Http.get_with("http://example.com", {"X-Test": "v\r\nInjected: 1"}))");
}

static void test_http_ssrf_loopback_fails() {
    // SSRF prevention blocks the IPv4 loopback range (127.0.0.0/8).
    ASSERT_EVAL_FAILURE(R"(Http.get("http://127.0.0.1/admin"))");
}

static void test_http_ssrf_private_10_fails() {
    // SSRF prevention blocks RFC 1918 10.0.0.0/8.
    ASSERT_EVAL_FAILURE(R"(Http.get("http://10.0.0.1/"))");
}

static void test_http_ssrf_private_192_168_fails() {
    // SSRF prevention blocks RFC 1918 192.168.0.0/16.
    ASSERT_EVAL_FAILURE(R"(Http.post("http://192.168.1.1/", "body"))");
}

static void test_http_request_missing_url_fails() {
    // Http.request with no 'url' option fails with a clear error.
    ASSERT_EVAL_FAILURE(R"(Http.request({"method": "GET"}, {}))");
}

// ─── Http: HTTPS scheme validation (negative — deterministic, no network) ─────
// HTTPS shares the http validation + SSRF pipeline. With TLS enabled these
// reach the host/CRLF/SSRF guards; with TLS disabled they are rejected up-front.
// Either way the result is a deterministic failure that needs no network.

static void test_http_https_empty_host_fails() {
    ASSERT_EVAL_FAILURE(R"(Http.get("https://"))");
}

static void test_http_https_crlf_path_injection_fails() {
    ASSERT_EVAL_FAILURE(R"(Http.get("https://example.com/a\r\nX-Injected: 1"))");
}

static void test_http_https_ssrf_private_fails() {
    ASSERT_EVAL_FAILURE(R"(Http.get("https://10.0.0.1/"))");
}

// ─── Http: URL parser malformed-port fallback (positive) ──────────────────────

static void test_http_parse_url_malformed_port_falls_back() {
    // A non-numeric port falls back to the scheme default (80 for http) rather
    // than throwing; locks the parse_port_or() fallback behaviour.
    const auto v = eval(R"(Http.parse_url("http://example.com:notaport/path"))");

    ASSERT_TRUE(v.is_record());

    const auto* port = v.as_record()->find_field("port");

    ASSERT_TRUE(port && port->is_string());
    ASSERT_EQ(port->as_string(), "80");
}

// ─── Http: response parsing (direct parse_response unit tests) ────────────────
// parse_response() frames the raw HTTP response with no network I/O, so these
// exercise the status-line and header parsing deterministically.

static void test_http_parse_response_status_reason_headers_body() {
    const auto resp =
        parse_response("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nhello world");

    ASSERT_EQ(resp.status_code, 200);
    ASSERT_EQ(resp.reason, "OK");
    ASSERT_EQ(resp.body, "hello world");
    ASSERT_EQ(resp.headers.size(), static_cast<std::size_t>(1));
    // Header names are lowercased and the leading space in the value is trimmed.
    ASSERT_EQ(resp.headers[0].first, "content-type");
    ASSERT_EQ(resp.headers[0].second, "text/plain");
}

static void test_http_parse_response_no_reason_phrase() {
    // A status line with no reason phrase still yields the status code.
    const auto resp = parse_response("HTTP/1.0 204\r\n\r\n");

    ASSERT_EQ(resp.status_code, 204);
    ASSERT_EQ(resp.reason, "");
}

static void test_http_parse_response_multiword_reason() {
    // The reason phrase keeps everything after the status code, spaces included.
    const auto resp = parse_response("HTTP/1.1 404 Not Found\r\n\r\n");

    ASSERT_EQ(resp.status_code, 404);
    ASSERT_EQ(resp.reason, "Not Found");
}

static void test_http_parse_response_malformed_status_code() {
    // A non-numeric status code yields a zero status and no reason (early out).
    const auto resp = parse_response("HTTP/1.1 abc Bad\r\n\r\nbody");

    ASSERT_EQ(resp.status_code, 0);
    ASSERT_EQ(resp.reason, "");
}

// ─── Http: Http.Request record + Http.send (typed request) ────────────────────

static void test_http_request_of_builds_typed_record() {
    // Http.request_of(method, url) yields an Http.Request record carrying the
    // Http.Method choice natively, with default headers/body/timeout.
    const auto v = eval(R"(Http.request_of(Http.Method.Get, "http://example.com/api"))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, "Request");

    const auto* method = v.as_record()->find_field("method");

    ASSERT_TRUE(method && method->is_choice());
    ASSERT_EQ(method->as_choice()->type_name, "Method");
    ASSERT_EQ(method->as_choice()->variant, "Get");

    const auto* url = v.as_record()->find_field("url");

    ASSERT_TRUE(url && url->is_string());
    ASSERT_EQ(url->as_string(), "http://example.com/api");

    const auto* headers = v.as_record()->find_field("headers");

    ASSERT_TRUE(headers && headers->is_dictionary());
    ASSERT_TRUE(headers->as_dictionary()->entries.empty());

    const auto* body = v.as_record()->find_field("body");

    ASSERT_TRUE(body && body->is_string());
    ASSERT_EQ(body->as_string(), "");

    const auto* timeout = v.as_record()->find_field("timeout_ms");

    ASSERT_TRUE(timeout && timeout->is_integer());
    ASSERT_EQ(timeout->as_integer(), static_cast<std::int64_t>(30000));
}

static void test_http_request_with_builds_full_record() {
    // Http.request_with(method, url, headers, body, timeout_ms) captures every field.
    const auto v = eval(
        R"(Http.request_with(Http.Method.Post, "http://example.com", {"X-Test": "1"}, "payload", 5000))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, "Request");

    const auto* method = v.as_record()->find_field("method");

    ASSERT_TRUE(method && method->is_choice());
    ASSERT_EQ(method->as_choice()->variant, "Post");

    const auto* headers = v.as_record()->find_field("headers");

    ASSERT_TRUE(headers && headers->is_dictionary());

    const auto* header = headers->as_dictionary()->find("X-Test");

    ASSERT_TRUE(header && header->is_string());
    ASSERT_EQ(header->as_string(), "1");

    const auto* body = v.as_record()->find_field("body");

    ASSERT_TRUE(body && body->is_string());
    ASSERT_EQ(body->as_string(), "payload");

    const auto* timeout = v.as_record()->find_field("timeout_ms");

    ASSERT_TRUE(timeout && timeout->is_integer());
    ASSERT_EQ(timeout->as_integer(), static_cast<std::int64_t>(5000));
}

static void test_http_request_with_clamps_negative_timeout() {
    // A negative timeout is clamped to 0.
    const auto v = eval(R"(Http.request_with(Http.Method.Get, "http://x", {}, "", -1).timeout_ms)");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), static_cast<std::int64_t>(0));
}

static void test_http_request_of_rejects_non_choice_method() {
    // A plain string is not an Http.Method choice — request_of rejects it.
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.request_of("GET", "http://x"))"));
}

static void test_http_send_rejects_non_request() {
    // Http.send only accepts an Http.Request record.
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.send("not a request"))"));
    ASSERT_TRUE(luma::test::eval_throws(R"(Http.send(Http.parse_url("http://example.com")))"));
}

static void test_http_send_reaches_request_pipeline() {
    // Http.send reads the Http.Method choice back to a verb and drives the request
    // pipeline; the SSRF guard blocks loopback deterministically (no network).
    ASSERT_EVAL_FAILURE(R"(Http.send(Http.request_of(Http.Method.Get, "http://127.0.0.1/admin")))");
}

static void test_http_send_empty_url_fails() {
    // A request with an empty url fails with a clear error before any network.
    ASSERT_EVAL_FAILURE(R"(Http.send(Http.request_of(Http.Method.Get, "")))");
}

static void test_http_request_functions_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Http.request_of"));
    ASSERT_TRUE(env->has("Http.request_with"));
    ASSERT_TRUE(env->has("Http.send"));
}

int main() {
    RUN(test_http_build_query);
    RUN(test_http_https_reaches_request_pipeline);
    RUN(test_http_module);
    RUN(test_http_parse_query);
    RUN(test_http_parse_url);
    RUN(test_http_basic_auth);
    RUN(test_http_basic_auth_empty_credentials);
    RUN(test_http_bearer_auth);
    RUN(test_http_bearer_auth_empty);
    RUN(test_http_method_to_string_all_verbs);
    RUN(test_http_method_to_string_rejects_non_choice);
    RUN(test_http_status_class_all_families);
    RUN(test_http_status_class_out_of_range_fails);
    RUN(test_http_is_success_reads_status);
    RUN(test_http_new_functions_registered);
    RUN(test_http_parse_url_default_http_port);
    RUN(test_http_parse_url_https_default_port);
    RUN(test_http_parse_url_ipv6);
    RUN(test_http_parse_url_scheme_lowercased);
    RUN(test_http_parse_url_no_path);
    RUN(test_http_build_query_encodes);
    RUN(test_http_build_query_empty);
    RUN(test_http_parse_query_decodes_and_empty_value);
    RUN(test_http_query_roundtrip_reserved_chars);
    RUN(test_http_parse_url_rejects_non_string);
    RUN(test_http_parse_query_rejects_non_string);
    RUN(test_http_build_query_rejects_non_dict);
    RUN(test_http_get_rejects_non_string);
    RUN(test_http_basic_auth_rejects_non_string);
    RUN(test_http_get_with_rejects_non_string_header_value);
    RUN(test_http_unsupported_scheme_fails);
    RUN(test_http_empty_host_fails);
    RUN(test_http_crlf_path_injection_fails);
    RUN(test_http_crlf_header_injection_fails);
    RUN(test_http_ssrf_loopback_fails);
    RUN(test_http_ssrf_private_10_fails);
    RUN(test_http_ssrf_private_192_168_fails);
    RUN(test_http_request_missing_url_fails);
    RUN(test_http_https_empty_host_fails);
    RUN(test_http_https_crlf_path_injection_fails);
    RUN(test_http_https_ssrf_private_fails);
    RUN(test_http_parse_url_malformed_port_falls_back);
    RUN(test_http_parse_response_status_reason_headers_body);
    RUN(test_http_parse_response_no_reason_phrase);
    RUN(test_http_parse_response_multiword_reason);
    RUN(test_http_parse_response_malformed_status_code);

    RUN(test_http_request_of_builds_typed_record);
    RUN(test_http_request_with_builds_full_record);
    RUN(test_http_request_with_clamps_negative_timeout);
    RUN(test_http_request_of_rejects_non_choice_method);
    RUN(test_http_send_rejects_non_request);
    RUN(test_http_send_reaches_request_pipeline);
    RUN(test_http_send_empty_url_fails);
    RUN(test_http_request_functions_registered);

    return SUMMARY();
}
