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

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
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

// Resolves an `Http.Method` choice variant to its uppercase HTTP verb.  Backs
// Http.method_to_string, which lets a program name a verb type-safely (autocomplete,
// exhaustive match, no typos) and feed it to the generic Http.request path — whose
// options travel in a homogeneous dictionary<string>, so the choice is converted to a
// string here rather than carried in the dictionary itself.  An unknown variant is
// impossible for a genuine Http.Method value (the type checker guarantees it); the throw
// guards against some other choice type being passed by mistake.
[[nodiscard]] std::string http_verb_from_method(std::string_view variant,
                                                const SourceLocation& loc) {
    // Variant names mirror the Http.Method choice in stdlib_type_arities.cpp.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> verbs{{
        {"Get", "GET"},
        {"Post", "POST"},
        {"Put", "PUT"},
        {"Patch", "PATCH"},
        {"Delete", "DELETE"},
        {"Head", "HEAD"},
        {"Options", "OPTIONS"},
    }};

    for (const auto& [name, verb] : verbs) {
        if (name == variant) {
            return std::string{verb};
        }
    }

    throw RuntimeError{
        std::format("Http.method_to_string: unknown HTTP method 'Http.Method.{}'", variant), loc,
        "use an Http.Method variant: Get, Post, Put, Patch, Delete, Head, Options"};
}

// Builds an empty dictionary<string> value, used for a request's default headers.
[[nodiscard]] Value make_empty_headers() {
    auto dict = std::make_shared<DictionaryValue>();
    dict->rebuild_index();

    return Value{std::move(dict)};
}

// Wraps an Http.StatusClass variant name in a ChoiceValue.  The runtime short
// name "StatusClass" matches how the type checker registers the choice from
// stdlib_type_arities.cpp; the five variant names must match that declaration.
[[nodiscard]] Value make_status_class_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "StatusClass";
    cv->variant = std::string{variant};

    return Value{std::move(cv)};
}

// Classifies a raw HTTP status code into its RFC 9110 family, or std::nullopt
// when the code is outside the valid 100-599 range.  Shared by Http.status_class
// and Http.is_success so the two never disagree on what "2xx" means.
[[nodiscard]] std::optional<std::string_view> status_class_variant(std::int64_t status) {
    if (status >= 100 && status < 200) {
        return "Informational";
    }
    if (status >= 200 && status < 300) {
        return "Success";
    }
    if (status >= 300 && status < 400) {
        return "Redirection";
    }
    if (status >= 400 && status < 500) {
        return "ClientError";
    }
    if (status >= 500 && status < 600) {
        return "ServerError";
    }

    return std::nullopt;
}

// Maps a status code to its standard reason phrase (RFC 9110 and common
// extensions).  Unknown codes fall back to a generic phrase for their class, or
// an empty string when even the class is out of range.  Shared by
// Http.status_text.
[[nodiscard]] std::string_view status_reason_phrase(std::int64_t status) {
    switch (status) {
        case 100:
            return "Continue";
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 204:
            return "No Content";
        case 206:
            return "Partial Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 415:
            return "Unsupported Media Type";
        case 418:
            return "I'm a Teapot";
        case 422:
            return "Unprocessable Content";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";
        default:
            break;
    }

    // Fall back to a generic phrase for the code's class.
    const auto variant = status_class_variant(status);

    if (!variant) {
        return "";
    }

    if (*variant == "Informational") {
        return "Informational";
    }
    if (*variant == "Success") {
        return "Success";
    }
    if (*variant == "Redirection") {
        return "Redirection";
    }
    if (*variant == "ClientError") {
        return "Client Error";
    }

    return "Server Error";
}

// Assembles an Http.Request record { method, url, headers, body, timeout_ms }.
// The record carries the Http.Method choice natively — no stringified verb — so a
// request stays typed and discoverable, mirroring how Http.Response is a record.
// Consumed by Http.send, which reads the choice back out with http_verb_from_method.
[[nodiscard]] Value make_request_record(Value method, std::string url, Value headers,
                                        std::string body, std::int64_t timeout_ms) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Request";
    rec->fields.emplace_back("method", std::move(method));
    rec->fields.emplace_back("url", Value{std::move(url)});
    rec->fields.emplace_back("headers", std::move(headers));
    rec->fields.emplace_back("body", Value{std::move(body)});
    rec->fields.emplace_back("timeout_ms", Value{timeout_ms});

    return Value{std::move(rec)};
}

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
        // Http.get_typed(url) -> result<Http.Response, Http.Error>
        // Opt-in typed-error variant of Http.get: a transport failure is surfaced as
        // an Http.Error choice (InvalidUrl / ConnectionFailed / Timeout / TlsError /
        // Blocked / Malformed) rather than an opaque string message, so a program can
        // match the category — retry only on Timeout, fall back only on
        // ConnectionFailed, distinguish a Blocked SSRF URL from a Malformed one —
        // instead of substring-matching the message.  Mirrors
        // FileSystem.read_file_typed / FileSystem.IoError; the string-error Http.get
        // is left untouched.
        .func("get_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.get_typed", loc);

            return do_http_request_typed("GET", args[0].as_string(), {}, {},
                                         k_http_default_timeout_ms, loc);
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

            // The "method" option is a string verb; a missing value defaults to GET.
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
        // Http.method_to_string(Http.Method) -> string
        // Converts an Http.Method choice to its uppercase HTTP verb, so a program can
        // name a verb type-safely and pass it under the "method" option of Http.request
        // (whose options dictionary holds strings, not choices).
        .func("method_to_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{
                    std::string{"Http.method_to_string: expected an Http.Method choice"}, loc,
                    "pass an Http.Method variant, e.g. Http.Method.Post"};
            }

            return Value{http_verb_from_method(args[0].as_choice()->variant, loc)};
        })
        // Http.status_class(status) -> result<Http.StatusClass>
        // Classifies a raw HTTP status code into its RFC 9110 family so a program can
        // match Http.StatusClass.Success / ClientError / … instead of hand-writing the
        // magic 200..300 boundaries.  Fails for a code outside the valid 100-599 range.
        .func("status_class", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto status = expect_integer(args[0], "Http.status_class", loc);
            const auto variant = status_class_variant(status);

            if (!variant) {
                return make_failure_value(
                    error_msg("Http", "status_class",
                              std::format("status {} is out of range (expected 100-599)", status)),
                    std::string{"invalid_argument"}, "Http.status_class");
            }

            return make_success_value(make_status_class_choice(*variant));
        })
        // Http.status_text(code) -> string
        // The standard reason phrase for a status code (404 -> "Not Found"), for
        // logging and user messages.  Unknown codes fall back to a generic class
        // phrase, or an empty string when the code is out of range.
        .func("status_text", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto status = expect_integer(args[0], "Http.status_text", loc);

            return Value{std::string{status_reason_phrase(status)}};
        })
        // Http.is_success(response) -> boolean
        // True when the response's status is a 2xx (Success) code — the type-safe,
        // self-documenting counterpart to hand-writing `response.status >= 200 &&
        // response.status < 300`.  Mirrors requests' .ok / Response.ok.
        .func("is_success", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{std::string{"Http.is_success: expected an Http.Response record"},
                                   loc, "pass the result of Http.get/post/send, e.g. via ??"};
            }

            const auto* status_val = args[0].as_record()->find_field("status");

            if (!status_val || !status_val->is_integer()) {
                return Value{false};
            }

            const auto status = status_val->as_integer();

            return Value{status >= 200 && status < 300};
        })
        // Http.request_of(method, url) -> Http.Request
        // Builds a typed request with empty headers, an empty body, and the default
        // timeout — the common case.  Use Http.request_with for full control.  The
        // Http.Method choice is stored directly (no stringifying), so the request is
        // discoverable and symmetrical with the Http.Response record.
        .func("request_of", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{std::string{"Http.request_of: expected an Http.Method choice"},
                                   loc, "pass an Http.Method variant, e.g. Http.Method.Get"};
            }

            const auto& url = expect_string(args[1], "Http.request_of", loc);

            return make_request_record(args[0], url, make_empty_headers(), std::string{},
                                       k_http_default_timeout_ms);
        })
        // Http.request_with(method, url, headers, body, timeout_ms) -> Http.Request
        // Builds a fully-specified typed request.  A negative timeout is clamped to 0.
        .func("request_with", 5)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{std::string{"Http.request_with: expected an Http.Method choice"},
                                   loc, "pass an Http.Method variant, e.g. Http.Method.Post"};
            }

            const auto& url = expect_string(args[1], "Http.request_with", loc);
            (void)expect_dict(args[2], "Http.request_with", loc);
            const auto& body = expect_string(args[3], "Http.request_with", loc);
            const auto timeout_ms = expect_integer(args[4], "Http.request_with", loc);

            return make_request_record(args[0], url, args[2], body,
                                       std::max<std::int64_t>(timeout_ms, 0));
        })
        // Http.send(request) -> result<Http.Response>
        // Performs the request described by an Http.Request record and returns the typed
        // response, mirroring Http.get/post but driven by a typed request rather than an
        // options dictionary.  The Http.Method choice is read back out here.
        .func("send", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record() || args[0].as_record()->type_name != "Request") {
                throw RuntimeError{
                    std::string{"Http.send: expected an Http.Request record"}, loc,
                    "build one with Http.request_of(method, url) or Http.request_with(...)"};
            }

            const auto& rec = *args[0].as_record();

            const auto* method_val = rec.find_field("method");
            const auto* url_val = rec.find_field("url");
            const auto* headers_val = rec.find_field("headers");
            const auto* body_val = rec.find_field("body");
            const auto* timeout_val = rec.find_field("timeout_ms");

            if (method_val == nullptr || !method_val->is_choice()) {
                throw RuntimeError{std::string{"Http.send: request has no valid method"}, loc,
                                   "build the request with Http.request_of / Http.request_with"};
            }

            const auto verb = http_verb_from_method(method_val->as_choice()->variant, loc);
            const auto url =
                (url_val != nullptr && url_val->is_string()) ? url_val->as_string() : std::string{};
            const auto body = (body_val != nullptr && body_val->is_string()) ? body_val->as_string()
                                                                             : std::string{};

            int timeout{k_http_default_timeout_ms};

            if (timeout_val != nullptr && timeout_val->is_integer()) {
                timeout = static_cast<int>(std::max<std::int64_t>(timeout_val->as_integer(), 0));
            }

            std::vector<std::pair<std::string, std::string>> headers;

            if (headers_val != nullptr && headers_val->is_dictionary()) {
                headers = extract_headers(*headers_val->as_dictionary(), loc);
            }

            if (url.empty()) {
                return make_failure_value(std::string{"Http.send: request has an empty url"});
            }

            return do_http_request(verb, url, body, headers, timeout, loc);
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

    // Named status-code constants for readable exact-code comparisons
    // (response.status == Http.status_not_found) instead of magic numbers.
    env->define("Http.status_ok", Value{std::int64_t{200}}, false);
    env->define("Http.status_created", Value{std::int64_t{201}}, false);
    env->define("Http.status_accepted", Value{std::int64_t{202}}, false);
    env->define("Http.status_no_content", Value{std::int64_t{204}}, false);
    env->define("Http.status_moved_permanently", Value{std::int64_t{301}}, false);
    env->define("Http.status_found", Value{std::int64_t{302}}, false);
    env->define("Http.status_not_modified", Value{std::int64_t{304}}, false);
    env->define("Http.status_bad_request", Value{std::int64_t{400}}, false);
    env->define("Http.status_unauthorized", Value{std::int64_t{401}}, false);
    env->define("Http.status_forbidden", Value{std::int64_t{403}}, false);
    env->define("Http.status_not_found", Value{std::int64_t{404}}, false);
    env->define("Http.status_method_not_allowed", Value{std::int64_t{405}}, false);
    env->define("Http.status_conflict", Value{std::int64_t{409}}, false);
    env->define("Http.status_too_many_requests", Value{std::int64_t{429}}, false);
    env->define("Http.status_server_error", Value{std::int64_t{500}}, false);
    env->define("Http.status_not_implemented", Value{std::int64_t{501}}, false);
    env->define("Http.status_bad_gateway", Value{std::int64_t{502}}, false);
    env->define("Http.status_service_unavailable", Value{std::int64_t{503}}, false);
    env->define("Http.status_gateway_timeout", Value{std::int64_t{504}}, false);

    register_http_parsing(env);
}
} // namespace luma
