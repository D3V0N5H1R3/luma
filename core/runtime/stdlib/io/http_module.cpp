// ─── http_module.cpp — Http module registration. ─────────────────────────────
//
// The Http module is split across several files for readability:
//   http_module.cpp          — module registration (this file)
//   http_module_request.cpp  — connection management, request building,
//                              response parsing, and request execution
//   http_module_parsing.cpp  — URL parsing, encoding, query string operations,
//                              and authentication helpers
//   http_security.cpp        — SSRF prevention, CRLF injection detection,
//                              and hostname validation
//
// Supporting headers:
//   http_module_connection.hpp — Connection I/O abstraction (plain + TLS)
//   http_module_request.hpp   — do_http_request() and extract_headers()
//   http_security.hpp         — security validation declarations
//   http_url_parser.hpp       — URL parser
//   common/url_codec.hpp      — URL percent-encoding

#include "runtime/stdlib/io/http_module.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <string>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/path_validator.hpp"
#include "runtime/stdlib/io/http_module_request.hpp"

namespace luma {

namespace {

// Default request timeout for the Http module.  Deliberately larger than the
// GraphicalUi HTTP commands' 8 s default, which run synchronously on the UI thread.
constexpr int k_http_default_timeout_ms = 30000;

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// ─── Module registration ────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

void register_http_ns(const EnvPtr& env) {
    ModuleBuilder{"Http", env} // Http.get(url) -> result<dictionary<string>>
        .func("get", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.get", loc);

            return do_http_request("GET", args[0].as_string(), {}, {}, k_http_default_timeout_ms,
                                   loc);
        })
        // Http.post(url, body) -> result<dictionary<string>>
        .func("post", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.post", loc);
            (void)expect_string(args[1], "Http.post", loc);

            return do_http_request("POST", args[0].as_string(), args[1].as_string(), {},
                                   k_http_default_timeout_ms, loc);
        })
        // Http.put(url, body) -> result<dictionary<string>>
        .func("put", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.put", loc);
            (void)expect_string(args[1], "Http.put", loc);

            return do_http_request("PUT", args[0].as_string(), args[1].as_string(), {},
                                   k_http_default_timeout_ms, loc);
        })
        // Http.delete(url) -> result<dictionary<string>>
        .func("delete", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.delete", loc);

            return do_http_request("DELETE", args[0].as_string(), {}, {}, k_http_default_timeout_ms,
                                   loc);
        })
        // Http.patch(url, body) -> result<dictionary<string>>
        .func("patch", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.patch", loc);
            (void)expect_string(args[1], "Http.patch", loc);

            return do_http_request("PATCH", args[0].as_string(), args[1].as_string(), {},
                                   k_http_default_timeout_ms, loc);
        })
        // Http.head(url) -> result<dictionary<string>>
        .func("head", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.head", loc);

            return do_http_request("HEAD", args[0].as_string(), {}, {}, k_http_default_timeout_ms,
                                   loc);
        })
        // Http.get_with(url, headers) -> result<dictionary<string>>
        .func("get_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.get_with", loc);
            (void)expect_dict(args[1], "Http.get_with", loc);

            auto headers = extract_headers(*args[1].as_dictionary(), loc);

            return do_http_request("GET", args[0].as_string(), {}, headers,
                                   k_http_default_timeout_ms, loc);
        })
        // Http.post_with(url, body, headers) -> result<dictionary<string>>
        .func("post_with", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.post_with", loc);
            (void)expect_string(args[1], "Http.post_with", loc);
            (void)expect_dict(args[2], "Http.post_with", loc);

            auto headers = extract_headers(*args[2].as_dictionary(), loc);

            return do_http_request("POST", args[0].as_string(), args[1].as_string(), headers,
                                   k_http_default_timeout_ms, loc);
        })
        // Http.delete_with(url, headers) -> result<dictionary<string>>
        .func("delete_with", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.delete_with", loc);
            (void)expect_dict(args[1], "Http.delete_with", loc);

            auto headers = extract_headers(*args[1].as_dictionary(), loc);

            return do_http_request("DELETE", args[0].as_string(), {}, headers,
                                   k_http_default_timeout_ms, loc);
        })
        // Http.put_with(url, body, headers) -> result<dictionary<string>>
        .func("put_with", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.put_with", loc);
            (void)expect_string(args[1], "Http.put_with", loc);
            (void)expect_dict(args[2], "Http.put_with", loc);

            auto headers = extract_headers(*args[2].as_dictionary(), loc);

            return do_http_request("PUT", args[0].as_string(), args[1].as_string(), headers,
                                   k_http_default_timeout_ms, loc);
        })
        // Http.patch_with(url, body, headers) -> result<dictionary<string>>
        .func("patch_with", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.patch_with", loc);
            (void)expect_string(args[1], "Http.patch_with", loc);
            (void)expect_dict(args[2], "Http.patch_with", loc);

            auto headers = extract_headers(*args[2].as_dictionary(), loc);

            return do_http_request("PATCH", args[0].as_string(), args[1].as_string(), headers,
                                   k_http_default_timeout_ms, loc);
        })
        // Http.request(options, headers) -> result<dictionary<string>>
        .func("request", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_dict(args[0], "Http.request", loc);
            (void)expect_dict(args[1], "Http.request", loc);

            const auto& opts = args[0].as_dictionary();
            const auto& hdrs = args[1].as_dictionary();

            auto get_opt = [&](const std::string& key) -> std::string {
                const auto* val = opts->find(key);

                if (val && val->is_string()) {
                    return val->as_string();
                }

                return {};
            };

            const auto method = get_opt("method").empty() ? std::string{"GET"} : get_opt("method");
            const auto url = get_opt("url");
            const auto body = get_opt("body");

            auto timeout_str = get_opt("timeout");

            int timeout{k_http_default_timeout_ms};

            if (!timeout_str.empty()) {
                try {
                    timeout = std::stoi(timeout_str);

                    timeout = std::max(timeout, 0);
                } catch (const std::invalid_argument&) { // NOLINT(bugprone-empty-catch)
                    // Malformed timeout value — use default.
                } catch (const std::out_of_range&) { // NOLINT(bugprone-empty-catch)
                    // Malformed timeout value — use default.
                }
            }

            auto headers = extract_headers(*hdrs, loc);

            // Content-Type from options
            const auto ct = get_opt("content_type");

            if (!ct.empty()) {
                headers.emplace_back("Content-Type", ct);
            }

            if (url.empty()) {
                return make_failure_value(std::string{"Http.request: missing 'url' in options"});
            }

            return do_http_request(method, url, body, headers, timeout, loc);
        })
        // Http.download(url, output_path) -> result<string>
        .func("download", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.download", loc);
            (void)expect_string(args[1], "Http.download", loc);

            const auto safe = validate_path(args[1].as_string(), loc);

            auto resp =
                do_http_request("GET", args[0].as_string(), {}, {}, k_http_default_timeout_ms, loc);

            if (!resp.is_result() || !resp.as_result()->is_success) {
                return resp;
            }

            // Extract body from response record.
            const auto& inner = resp.as_result()->owned_inner;

            if (!inner->is_record()) {
                return make_failure_value(std::string{"Http.download: unexpected response format"});
            }

            const auto* status_val = inner->as_record()->find_field("status");

            if (status_val && status_val->is_integer()) {
                const auto status_code = status_val->as_integer();

                if (status_code < 200 || status_code >= 300) {
                    return make_failure_value(error_msg(
                        "Http", "download", std::format("server returned status {}", status_code)));
                }
            }

            const auto* body_val = inner->as_record()->find_field("body");

            if (!body_val || !body_val->is_string()) {
                return make_failure_value(std::string{"Http.download: no body in response"});
            }

            std::ofstream out{safe, std::ios::binary};

            if (!out.is_open()) {
                return make_failure_value(
                    error_msg("Http", "download", std::format("cannot write '{}'", safe.string())));
            }

            const auto& body = body_val->as_string();

            out.write(body.data(), static_cast<std::streamsize>(body.size()));

            return make_success_value(Value{safe.string()});
        });

    register_http_parsing(env);
}
} // namespace luma
