// Commonly used Luma code snippets for LSP tests.

#ifndef LUMA_LSP_TEST_FIXTURES_HPP
#define LUMA_LSP_TEST_FIXTURES_HPP

namespace luma::lsp::test_fixtures {

namespace simple {

inline constexpr const char* k_main = "@main\n"
                                      "function main() {\n"
                                      "    x = 42\n"
                                      "}\n";

inline constexpr const char* k_main_with_usage = "@main\n"
                                                 "function main() {\n"
                                                 "    x = 42\n"
                                                 "    y = x + 1\n"
                                                 "}\n";

inline constexpr const char* k_empty_main = "@main\n"
                                            "function main() {\n"
                                            "}\n";

} // namespace simple

namespace types {

inline constexpr const char* k_record_point = "record Point {\n"
                                              "    x: integer\n"
                                              "    y: integer\n"
                                              "}\n";

inline constexpr const char* k_record_point_with_main = "record Point {\n"
                                                        "    x: integer\n"
                                                        "    y: integer\n"
                                                        "}\n"
                                                        "\n"
                                                        "@main\n"
                                                        "function main() {\n"
                                                        "    p = Point { x = 1, y = 2 }\n"
                                                        "}\n";

inline constexpr const char* k_choice_color = "choice Color {\n"
                                              "    Red\n"
                                              "    Green\n"
                                              "    Blue\n"
                                              "}\n";

inline constexpr const char* k_interface_shape_with_circle = "interface Shape {\n"
                                                             "    area: function(): number\n"
                                                             "}\n"
                                                             "\n"
                                                             "record Circle {\n"
                                                             "    radius: number\n"
                                                             "} implements Shape {\n"
                                                             "    function area(): number {\n"
                                                             "        return 3.14\n"
                                                             "    }\n"
                                                             "}\n";

} // namespace types

namespace functions {

inline constexpr const char* k_function_with_params =
    "function add(a: integer, b: integer) -> integer {\n"
    "    return a + b\n"
    "}\n";

inline constexpr const char* k_greet_function = "function greet(name: string) -> string {\n"
                                                "    return \"Hello, ${name}\"\n"
                                                "}\n";

inline constexpr const char* k_greet_with_main = "function greet(name: string) -> string {\n"
                                                 "    return \"Hello, ${name}\"\n"
                                                 "}\n"
                                                 "\n"
                                                 "@main\n"
                                                 "function main() {\n"
                                                 "    greet(\"world\")\n"
                                                 "}\n";

inline constexpr const char* k_helper_with_main = "function helper(): integer {\n"
                                                  "    return 1\n"
                                                  "}\n"
                                                  "\n"
                                                  "@main\n"
                                                  "function main() {\n"
                                                  "    x = helper()\n"
                                                  "}\n";

inline constexpr const char* k_namespace_utils =
    "namespace Utils {\n"
    "    function add(a: integer, b: integer) -> integer {\n"
    "        return a + b\n"
    "    }\n"
    "\n"
    "    function sub(a: integer, b: integer) -> integer {\n"
    "        return a - b\n"
    "    }\n"
    "}\n";

} // namespace functions

} // namespace luma::lsp::test_fixtures

#endif // LUMA_LSP_TEST_FIXTURES_HPP
