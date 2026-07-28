// Http module — URL parsing, encoding, query string operations, and
// authentication helpers.
// Split from http_module.cpp for readability.  Registered by
// register_http_parsing() called from register_http_ns().

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/base64_codec.hpp"
#include "common/url_codec.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/http_module.hpp"
#include "runtime/stdlib/io/http_url_parser.hpp"

namespace luma {

namespace {

// Parse query string "a=b&c=d" into a dictionary.
[[nodiscard]] std::shared_ptr<DictionaryValue> parse_query_string(const std::string& qs) {
    auto dict = std::make_shared<DictionaryValue>();

    if (qs.empty()) {
        return dict;
    }

    // Pre-build the empty hash index so each set() below is O(1), keeping query
    // string parsing O(n) in the number of parameters rather than O(n^2).
    dict->rebuild_index();

    std::size_t pos{0};

    while (pos < qs.size()) {
        auto amp = qs.find('&', pos);

        if (amp == std::string::npos) {
            amp = qs.size();
        }

        const auto pair = qs.substr(pos, amp - pos);

        auto eq = pair.find('=');

        if (eq != std::string::npos) {
            dict->set(url_decode(pair.substr(0, eq)).value_or(std::string{}),
                      Value{url_decode(pair.substr(eq + 1)).value_or(std::string{})});
        } else {
            dict->set(url_decode(pair).value_or(std::string{}), Value{std::string{}});
        }

        pos = amp + 1;
    }

    return dict;
}

} // anonymous namespace

// ─── Cookie parsing / formatting helpers (Http.Cookie) ───────────────────────

namespace {

// Trim leading and trailing ASCII whitespace.
[[nodiscard]] std::string trim(std::string_view s) {
    const auto not_space = [](unsigned char c) {
        return std::isspace(c) == 0;
    };

    const auto begin = std::ranges::find_if(s, not_space);
    const auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();

    if (begin >= end) {
        return {};
    }

    return std::string{begin, end};
}

// Lowercase an ASCII string, for case-insensitive attribute-name comparison.
[[nodiscard]] std::string to_lower(std::string_view s) {
    std::string out{s};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return out;
}

// Build an Http.Cookie record.  The short runtime type_name "Cookie" matches the
// "Http.Cookie" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_cookie(std::string name, std::string value, std::string domain,
                                std::string path, std::string expires, bool secure,
                                bool http_only) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Cookie";
    rec->fields.emplace_back("name", Value{std::move(name)});
    rec->fields.emplace_back("value", Value{std::move(value)});
    rec->fields.emplace_back("domain", Value{std::move(domain)});
    rec->fields.emplace_back("path", Value{std::move(path)});
    rec->fields.emplace_back("expires", Value{std::move(expires)});
    rec->fields.emplace_back("secure", Value{secure});
    rec->fields.emplace_back("http_only", Value{http_only});

    return Value{std::move(rec)};
}

// Parse a Set-Cookie header value into an Http.Cookie.  The first "name=value"
// pair is the cookie; subsequent ';'-separated segments are attributes (Domain,
// Path, Expires, plus the valueless Secure / HttpOnly flags).  Lenient: unknown
// attributes are ignored.  Fails only when the mandatory first name is empty.
[[nodiscard]] Value parse_cookie_header(const std::string& header) {
    std::vector<std::string> parts;

    std::size_t pos{0};
    while (pos <= header.size()) {
        const auto semi = header.find(';', pos);
        const auto end = semi == std::string::npos ? header.size() : semi;
        parts.push_back(header.substr(pos, end - pos));

        if (semi == std::string::npos) {
            break;
        }

        pos = semi + 1;
    }

    if (parts.empty()) {
        return make_failure_value(
            error_msg("Http", "parse_cookie", "expected a name=value cookie pair"));
    }

    const auto& first = parts.front();
    const auto eq = first.find('=');

    if (eq == std::string::npos) {
        return make_failure_value(
            error_msg("Http", "parse_cookie", "expected a name=value cookie pair"));
    }

    std::string name = trim(std::string_view{first}.substr(0, eq));
    std::string value = trim(std::string_view{first}.substr(eq + 1));

    if (name.empty()) {
        return make_failure_value(
            error_msg("Http", "parse_cookie", "the cookie name must not be empty"));
    }

    std::string domain;
    std::string path;
    std::string expires;
    bool secure = false;
    bool http_only = false;

    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto attr = trim(parts[i]);

        if (attr.empty()) {
            continue;
        }

        const auto attr_eq = attr.find('=');

        if (attr_eq == std::string::npos) {
            const auto flag = to_lower(attr);

            if (flag == "secure") {
                secure = true;
            } else if (flag == "httponly") {
                http_only = true;
            }

            continue;
        }

        const auto key = to_lower(trim(std::string_view{attr}.substr(0, attr_eq)));
        std::string attr_value = trim(std::string_view{attr}.substr(attr_eq + 1));

        if (key == "domain") {
            domain = std::move(attr_value);
        } else if (key == "path") {
            path = std::move(attr_value);
        } else if (key == "expires") {
            expires = std::move(attr_value);
        }
    }

    return make_success_value(make_cookie(std::move(name), std::move(value), std::move(domain),
                                          std::move(path), std::move(expires), secure, http_only));
}

// Build an Http.MediaType record.  The short runtime type_name "MediaType"
// matches the "Http.MediaType" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_media_type(std::string type, std::string subtype,
                                    std::shared_ptr<DictionaryValue> parameters) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "MediaType";
    rec->fields.emplace_back("type", Value{std::move(type)});
    rec->fields.emplace_back("subtype", Value{std::move(subtype)});
    rec->fields.emplace_back("parameters", Value{std::move(parameters)});

    return Value{std::move(rec)};
}

// Strip one layer of surrounding double quotes from a parameter value (RFC 9110
// quoted-string), leaving unquoted values untouched.
[[nodiscard]] std::string unquote(std::string_view s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return std::string{s.substr(1, s.size() - 2)};
    }

    return std::string{s};
}

// Parse a Content-Type header value ("type/subtype; key=value; ...") into an
// Http.MediaType.  `type`/`subtype` are lower-cased (RFC 9110 case-insensitive)
// and parameter keys lower-cased; parameter values keep their case (a quoted
// value is unquoted).  Lenient on the parameter list (a malformed parameter is
// skipped) but fails when the essential "type/subtype" form is absent.
[[nodiscard]] Value parse_media_type_header(const std::string& header) {
    // The media type is everything before the first ';'; the remainder is the
    // parameter list.
    const auto semi = header.find(';');
    const auto essence = trim(std::string_view{header}.substr(0, semi));

    const auto slash = essence.find('/');

    if (slash == std::string::npos) {
        return make_failure_value(
            error_msg("Http", "parse_media_type", "expected a 'type/subtype' media type"));
    }

    std::string type = to_lower(trim(std::string_view{essence}.substr(0, slash)));
    std::string subtype = to_lower(trim(std::string_view{essence}.substr(slash + 1)));

    if (type.empty() || subtype.empty()) {
        return make_failure_value(
            error_msg("Http", "parse_media_type", "the type and subtype must not be empty"));
    }

    auto parameters = std::make_shared<DictionaryValue>();

    std::size_t pos = semi == std::string::npos ? header.size() : semi + 1;

    while (pos < header.size()) {
        const auto next = header.find(';', pos);
        const auto end = next == std::string::npos ? header.size() : next;
        const std::string_view segment{header.data() + pos, end - pos};

        const auto eq = segment.find('=');

        if (eq != std::string_view::npos) {
            std::string key = to_lower(trim(segment.substr(0, eq)));
            std::string value = unquote(trim(segment.substr(eq + 1)));

            if (!key.empty()) {
                parameters->set(std::move(key), Value{std::move(value)});
            }
        }

        if (next == std::string::npos) {
            break;
        }

        pos = next + 1;
    }

    return make_success_value(
        make_media_type(std::move(type), std::move(subtype), std::move(parameters)));
}

} // anonymous namespace

void register_http_parsing(const EnvPtr& env) {
    ModuleBuilder{"Http", env} // Http.basic_auth(user, pass) -> string
        .func("basic_auth", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& user = expect_string(args[0], "Http.basic_auth", loc);
            const auto& pass = expect_string(args[1], "Http.basic_auth", loc);

            const auto credentials = user + ":" + pass;

            return Value{std::string{"Basic "} + base64_encode(credentials)};
        })
        // Http.bearer_auth(token) -> string
        .func("bearer_auth", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& token = expect_string(args[0], "Http.bearer_auth", loc);

            return Value{std::string{"Bearer "} + token};
        })
        // Http.authorization_header(auth) -> string
        // Renders an Http.Auth choice into its Authorization header value: a
        // Basic(username, password) becomes "Basic <base64(username:password)>" and a
        // Bearer(token) becomes "Bearer <token>".  The typed choice makes a scheme
        // typo a compile error, mirroring Http.method_to_string over Http.Method; the
        // free basic_auth / bearer_auth helpers remain for the stringly-typed path.
        .func("authorization_header", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{
                    std::string{"Http.authorization_header: expected an Http.Auth choice"}, loc,
                    "pass Http.Auth.Basic(user, pass) or Http.Auth.Bearer(token)"};
            }

            const auto& auth = *args[0].as_choice();

            const auto field_string = [&](std::size_t index) -> std::string {
                return index < auth.fields.size() && auth.fields[index].is_string()
                           ? auth.fields[index].as_string()
                           : std::string{};
            };

            if (auth.variant == "Basic") {
                const auto credentials = field_string(0) + ":" + field_string(1);

                return Value{std::string{"Basic "} + base64_encode(credentials)};
            }

            if (auth.variant == "Bearer") {
                return Value{std::string{"Bearer "} + field_string(0)};
            }

            throw RuntimeError{
                std::format("Http.authorization_header: unknown Http.Auth variant '{}'",
                            auth.variant),
                loc, "use Http.Auth.Basic(user, pass) or Http.Auth.Bearer(token)"};
        })
        // Http.parse_url(url) -> dictionary<string>
        .func("parse_url", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.parse_url", loc);

            const auto parsed = parse_url(args[0].as_string());

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "UrlParts";
            rec->fields.emplace_back("scheme", Value{parsed.scheme});
            rec->fields.emplace_back("host", Value{parsed.host});
            rec->fields.emplace_back("port", Value{std::to_string(parsed.port)});
            rec->fields.emplace_back("path", Value{parsed.path});
            rec->fields.emplace_back("query", Value{parsed.query});

            return Value{std::move(rec)};
        })
        // Http.build_url(parts: Http.UrlParts) -> string
        // The inverse of parse_url: reassemble scheme/host/port/path/query into a
        // URL string.  A port equal to the scheme's default (80 for http, 443 for
        // https) or "0"/empty is omitted; a non-empty query is appended after "?".
        .func("build_url", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{std::string{"Http.build_url: expected an Http.UrlParts record"},
                                   loc, "pass the result of Http.parse_url"};
            }

            const auto& rec = *args[0].as_record();

            const auto field = [&](std::string_view name) -> std::string {
                const auto* v = rec.find_field(name);
                return (v != nullptr && v->is_string()) ? v->as_string() : std::string{};
            };

            const std::string scheme = field("scheme");
            const std::string host = field("host");
            const std::string port = field("port");
            const std::string path = field("path");
            const std::string query = field("query");

            std::string url{};

            if (!scheme.empty()) {
                url += scheme + "://";
            }

            url += host;

            // Omit the port when it is empty, "0", or the scheme's default.
            const bool default_port = (scheme == "http" && port == "80") ||
                                      (scheme == "https" && port == "443");

            if (!port.empty() && port != "0" && !default_port) {
                url += ":" + port;
            }

            url += path;

            if (!query.empty()) {
                url += "?" + query;
            }

            return Value{std::move(url)};
        })
        .func("build_query", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_dict(args[0], "Http.build_query", loc);

            std::string qs{};

            for (const auto& [k, v] : args[0].as_dictionary()->entries) {
                if (!qs.empty()) {
                    qs += '&';
                }

                qs += url_encode(k) + "=" + url_encode(v.to_string());
            }

            return Value{qs};
        })
        // Http.parse_query(string) -> dictionary<string>
        .func("parse_query", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Http.parse_query", loc);

            return Value{parse_query_string(args[0].as_string())};
        })
        // Http.parse_cookie(header) -> result<Http.Cookie>
        .func("parse_cookie", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& header = expect_string(args[0], "Http.parse_cookie", loc);

            return parse_cookie_header(header);
        })
        // Http.parse_media_type(header) -> result<Http.MediaType>
        // Parse a Content-Type header value into a structured record so "is this
        // JSON?" or reading the charset no longer needs manual string splitting.
        // Mirrors Http.parse_url / Http.parse_cookie.
        .func("parse_media_type", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& header = expect_string(args[0], "Http.parse_media_type", loc);

            return parse_media_type_header(header);
        })
        // Http.cookie_header(cookie) -> string
        .func("cookie_header", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_record()) {
                throw RuntimeError{"Http.cookie_header: expected an Http.Cookie record", loc,
                                   "build one with Http.parse_cookie(header)"};
            }

            const auto& rec = args[0].as_record();

            const auto str_field = [&](const char* name) -> std::string {
                const Value* f = rec->find_field(name);
                return (f != nullptr && f->is_string()) ? f->as_string() : std::string{};
            };
            const auto bool_field = [&](const char* name) -> bool {
                const Value* f = rec->find_field(name);
                return f != nullptr && f->is_bool() && f->as_bool();
            };

            std::string out = str_field("name") + "=" + str_field("value");

            const auto domain = str_field("domain");
            const auto path = str_field("path");
            const auto expires = str_field("expires");

            if (!domain.empty()) {
                out += "; Domain=" + domain;
            }
            if (!path.empty()) {
                out += "; Path=" + path;
            }
            if (!expires.empty()) {
                out += "; Expires=" + expires;
            }
            if (bool_field("secure")) {
                out += "; Secure";
            }
            if (bool_field("http_only")) {
                out += "; HttpOnly";
            }

            return Value{std::move(out)};
        });
}

} // namespace luma
