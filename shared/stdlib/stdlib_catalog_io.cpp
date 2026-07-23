#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_console_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p) {
    append_specs(specs,
                 {
                     m.fn("prompt", 1, "(message: string)", R::result_string(), {p.string}),
                     m.fn("read_from_stdin", 0, "()", R::result_string(), {}),
                     m.fn("write_to_stderr", 1, "(text: string)", R::result_boolean(), {p.string}),
                     m.fn("write_to_stdout", 1, "(text: string)", R::result_boolean(), {p.string}),
                 });
}

void register_file_system_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p) {
    append_specs(
        specs, {
                   m.fn("absolute_path", 1, "(path: string)", R::result_string(), {p.string}),
                   m.fn("append_file", 2, "(path: string, content: string)", R::result_boolean(),
                        {p.string, p.string}),
                   m.fn("copy", 2, "(from: string, to: string)", R::result_boolean(),
                        {p.string, p.string}),
                   m.fn("create_directory", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("delete", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("delete_directory", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("exists", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("extension", 1, "(path: string)", R::string_type(), {p.string}),
                   m.fn("get_modified_time", 1, "(path: string)", R::result_number(), {p.string}),
                   m.fn("home_directory", 0, "()", R::result_string(), {}),
                   m.fn("is_absolute", 1, "(path: string)", R::boolean_type(), {p.string}),
                   m.fn("is_directory", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("is_file", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.fn("is_relative", 1, "(path: string)", R::boolean_type(), {p.string}),
                   m.fn("is_symlink", 1, "(path: string)", R::result_boolean(), {p.string}),
                   m.variadic_fn("join", 2, "(parts: string...)", R::string_type(), {p.string}),
                   m.fn("list_directories", 1, "(path: string)",
                        R::result(R::array(R::string_type())), {p.string}),
                   m.fn("list_files", 1, "(path: string)", R::result(R::array(R::string_type())),
                        {p.string}),
                   m.fn("list_recursively", 1, "(path: string)",
                        R::result(R::array(R::string_type())), {p.string}),
                   m.fn("metadata", 1, "(path: string)", R::result(named::file_info()), {p.string}),
                   m.fn("name", 1, "(path: string)", R::string_type(), {p.string}),
                   m.fn("normalize", 1, "(path: string)", R::string_type(), {p.string}),
                   m.fn("parent", 1, "(path: string)", R::string_type(), {p.string}),
                   m.fn("read_file", 1, "(path: string)", R::result_string(), {p.string}),
                   m.fn("read_file_limited", 2, "(path: string, max_bytes: integer)",
                        R::result_string(), {p.string, p.integer}),
                   m.fn("read_lines", 1, "(path: string)", R::result(R::array(R::string_type())),
                        {p.string}),
                   m.fn("relative", 2, "(path: string, base: string)", R::string_type(),
                        {p.string, p.string}),
                   m.fn("rename", 2, "(from: string, to: string)", R::result_boolean(),
                        {p.string, p.string}),
                   m.fn("rename_directory", 2, "(from: string, to: string)", R::result_boolean(),
                        {p.string, p.string}),
                   m.fn("size", 1, "(path: string)", R::result_integer(), {p.string}),
                   m.fn("split_path", 1, "(path: string)", named::path_parts(), {p.string}),
                   m.fn("stem", 1, "(path: string)", R::string_type(), {p.string}),
                   m.fn("write_file", 2, "(path: string, content: string)", R::result_boolean(),
                        {p.string, p.string}),
                   m.fn("write_lines", 2, "(path: string, lines: array<string>)",
                        R::result_boolean(), {p.string, p.array_string}),
               });
}

void register_process_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("current_directory", 0, "()", R::result_string(), {}),
            m.fn("exit", 1, "(code: integer)", R::void_type(), {p.integer}),
            m.fn("get_arguments", 0, "()", R::array_string(), {}),
            m.fn("get_all_environment_variables", 0, "()", R::dict_string(), {}),
            m.fn("get_environment_variable", 1, "(name: string)", R::result_string(), {p.string}),
            m.fn("get_process_id", 0, "()", R::integer_type(), {}),
            m.fn("has_environment_variable", 1, "(name: string)", R::boolean_type(), {p.string}),
            m.fn("run", 1, "(command: string)", R::result(named::process_result()), {p.string}),
            m.fn("set_environment_variable", 2, "(name: string, value: string)", R::result_void(),
                 {p.string, p.string}),
        });
}

void register_socket_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                               const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("accept", 1, "(s: socket)", R::result(named::socket()), {p.socket}),
            m.fn("close", 1, "(s: socket)", R::void_type(), {p.socket}),
            m.fn("connect", 2, "(host: string, port: integer)", R::result(named::socket()),
                 {p.string, p.integer}),
            m.fn("is_connected", 1, "(s: socket)", R::boolean_type(), {p.socket}),
            m.fn("listen", 2, "(host: string, port: integer)", R::result(named::socket()),
                 {p.string, p.integer}),
            m.fn("local_address", 1, "(s: socket)", R::result_string(), {p.socket}),
            m.fn("local_address_parts", 1, "(s: socket)", R::result(named::address()), {p.socket}),
            m.fn("receive", 2, "(s: socket, max_bytes: integer)", R::result_string(),
                 {p.socket, p.integer}),
            m.fn("remote_address", 1, "(s: socket)", R::result_string(), {p.socket}),
            m.fn("remote_address_parts", 1, "(s: socket)", R::result(named::address()), {p.socket}),
            m.fn("send", 2, "(s: socket, data: string)", R::result_integer(), {p.socket, p.string}),
            m.fn("set_timeout", 2, "(s: socket, ms: integer)", R::result_boolean(),
                 {p.socket, p.integer}),
            m.fn("udp_bind", 3, "(s: socket, host: string, port: integer)", R::result_boolean(),
                 {p.socket, p.string, p.integer}),
            m.fn("udp_create", 0, "()", R::result(named::socket()), {}),
            m.fn("udp_receive", 2, "(s: socket, max_bytes: integer)",
                 R::result(named::udp_packet()), {p.socket, p.integer}),
            m.fn("udp_send", 4, "(s: socket, data: string, host: string, port: integer)",
                 R::result_integer(), {p.socket, p.string, p.string, p.integer}),
        });
}

void register_http_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("basic_auth", 2, "(user: string, pass: string)", R::string_type(),
                 {p.string, p.string}),
            m.fn("bearer_auth", 1, "(token: string)", R::string_type(), {p.string}),
            m.fn("build_query", 1, "(params: dictionary<string>)", R::string_type(), {p.dict_any}),
            m.fn("delete", 1, "(url: string)", R::result(named::response()), {p.string}),
            m.fn("delete_with", 2, "(url: string, headers: dictionary<string>)",
                 R::result(named::response()), {p.string, p.dict_any}),
            m.fn("download", 2, "(url: string, path: string)", R::result_string(),
                 {p.string, p.string}),
            m.fn("get", 1, "(url: string)", R::result(named::response()), {p.string}),
            m.fn("get_with", 2, "(url: string, headers: dictionary<string>)",
                 R::result(named::response()), {p.string, p.dict_any}),
            m.fn("head", 1, "(url: string)", R::result(named::response()), {p.string}),
            m.fn("method_to_string", 1, "(method: Http.Method)", R::string_type(), {p.any}),
            m.fn("parse_query", 1, "(query: string)", R::dict_string(), {p.string}),
            m.fn("parse_url", 1, "(url: string)", named::url_parts(), {p.string}),
            m.fn("patch", 2, "(url: string, body: string)", R::result(named::response()),
                 {p.string, p.string}),
            m.fn("patch_with", 3, "(url: string, body: string, headers: dictionary<string>)",
                 R::result(named::response()), {p.string, p.string, p.dict_any}),
            m.fn("post", 2, "(url: string, body: string)", R::result(named::response()),
                 {p.string, p.string}),
            m.fn("post_with", 3, "(url: string, body: string, headers: dictionary<string>)",
                 R::result(named::response()), {p.string, p.string, p.dict_any}),
            m.fn("put", 2, "(url: string, body: string)", R::result(named::response()),
                 {p.string, p.string}),
            m.fn("put_with", 3, "(url: string, body: string, headers: dictionary<string>)",
                 R::result(named::response()), {p.string, p.string, p.dict_any}),
            m.fn("request", 2, "(options: dictionary, headers: dictionary)",
                 R::result(named::response()), {p.dict_any, p.dict_any}),
            m.fn("request_of", 2, "(method: Http.Method, url: string)", named::request(),
                 {p.any, p.string}),
            m.fn("request_with",
                 5, "(method: Http.Method, url: string, headers: dictionary<string>, "
                    "body: string, timeout_ms: integer)",
                 named::request(), {p.any, p.string, p.dict_any, p.string, p.integer}),
            m.fn("send", 1, "(request: Http.Request)", R::result(named::response()), {p.any}),
        });
}

void register_key_value_store_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                        const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("clear", 1, "(store: key_value_store)", R::result(named::key_value_store()),
                 {p.kv_store}),
            m.fn("count", 1, "(store: key_value_store)", R::integer_type(), {p.kv_store}),
            m.fn("destroy", 1, "(store: key_value_store)", R::result_string(), {p.kv_store}),
            m.fn("find_by_pattern", 2, "(store: key_value_store, pattern: string)",
                 R::dict_string(), {p.kv_store, p.string}),
            m.fn("get", 2, "(store: key_value_store, key: string)", R::result_string(),
                 {p.kv_store, p.string}),
            m.fn("get_many", 2, "(store: key_value_store, keys: array<string>)", R::dict_string(),
                 {p.kv_store, p.array_string}),
            m.fn("has", 2, "(store: key_value_store, key: string)", R::boolean_type(),
                 {p.kv_store, p.string}),
            m.fn("is_read_only", 1, "(store: key_value_store)", R::boolean_type(), {p.kv_store}),
            m.fn("keys", 1, "(store: key_value_store)", R::array_string(), {p.kv_store}),
            m.fn("open", 1, "(path: string)", R::result(named::key_value_store()), {p.string}),
            m.fn("open_read_only", 1, "(path: string)", R::result(named::key_value_store()),
                 {p.string}),
            m.fn("reload", 1, "(store: key_value_store)", R::result(named::key_value_store()),
                 {p.kv_store}),
            m.fn("remove", 2, "(store: key_value_store, key: string)",
                 R::result(named::key_value_store()), {p.kv_store, p.string}),
            m.fn("save", 1, "(store: key_value_store)", R::result_string(), {p.kv_store}),
            m.fn("set", 3, "(store: key_value_store, key: string, value: string)",
                 R::result(named::key_value_store()), {p.kv_store, p.string, p.string}),
            m.fn("set_many", 2, "(store: key_value_store, entries: dictionary<string>)",
                 R::result(named::key_value_store()), {p.kv_store, p.dict_any}),
            m.fn("to_dictionary", 1, "(store: key_value_store)", R::dict_string(), {p.kv_store}),
            m.fn("values", 1, "(store: key_value_store)", R::array_string(), {p.kv_store}),
        });
}

} // namespace luma::stdlib::detail
