// Http module — URL parsing, encoding, query string operations, and
// authentication helpers.
// Split from http_module.cpp for readability.  Registered by
// register_http_parsing() called from register_http_ns().

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/base64_codec.hpp"
#include "common/url_codec.hpp"
#include "runtime/interpreter/value.hpp"
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
        // Http.build_query(dictionary) -> string
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
        });
}

} // namespace luma
