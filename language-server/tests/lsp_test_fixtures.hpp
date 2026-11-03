// Commonly used Luma code snippets for LSP tests.

#ifndef LUMA_LSP_TEST_FIXTURES_HPP
#define LUMA_LSP_TEST_FIXTURES_HPP

namespace luma::lsp::test_fixtures {

namespace simple {

inline constexpr const char* k_main = "@main\n"
                                      "function void main() {\n"
                                      "    integer x = 42\n"
                                      "}\n";

inline constexpr const char* k_main_with_usage = "@main\n"
                                                 "function void main() {\n"
                                                 "    integer x = 42\n"
                                                 "    integer y = x + 1\n"
                                                 "}\n";

inline constexpr const char* k_empty_main = "@main\n"
                                            "function void main() {\n"
                                            "}\n";

} // namespace simple

namespace types {

inline constexpr const char* k_record_point = "record Point {\n"
                                              "    integer x,\n"
                                              "    integer y\n"
                                              "}\n";

inline constexpr const char* k_record_point_with_main = "record Point {\n"
                                                        "    integer x,\n"
                                                        "    integer y\n"
                                                        "}\n"
                                                        "\n"
                                                        "@main\n"
                                                        "function void main() {\n"
                                                        "    Point p = Point { x = 1, y = 2 }\n"
                                                        "}\n";

inline constexpr const char* k_choice_color = "choice Color {\n"
                                              "    Red\n"
                                              "    Green\n"
                                              "    Blue\n"
                                              "}\n";

// Interfaces are satisfied structurally: Circle satisfies Shape because it has
// every field Shape declares.
inline constexpr const char* k_interface_shape_with_circle = "interface Shape {\n"
                                                             "    number area\n"
                                                             "}\n"
                                                             "\n"
                                                             "record Circle {\n"
                                                             "    number radius,\n"
                                                             "    number area\n"
                                                             "}\n";

} // namespace types

namespace functions {

inline constexpr const char* k_function_with_params =
    "function integer add(integer a, integer b) {\n"
    "    return a + b\n"
    "}\n";

inline constexpr const char* k_greet_function = "function string greet(string name) {\n"
                                                "    return \"Hello, ${name}\"\n"
                                                "}\n";

inline constexpr const char* k_greet_with_main = "function string greet(string name) {\n"
                                                 "    return \"Hello, ${name}\"\n"
                                                 "}\n"
                                                 "\n"
                                                 "@main\n"
                                                 "function void main() {\n"
                                                 "    greet(\"world\")\n"
                                                 "}\n";

inline constexpr const char* k_helper_with_main = "function integer helper() {\n"
                                                  "    return 1\n"
                                                  "}\n"
                                                  "\n"
                                                  "@main\n"
                                                  "function void main() {\n"
                                                  "    integer x = helper()\n"
                                                  "}\n";

inline constexpr const char* k_namespace_utils =
    "namespace Utils {\n"
    "    function integer add(integer a, integer b) {\n"
    "        return a + b\n"
    "    }\n"
    "\n"
    "    function integer sub(integer a, integer b) {\n"
    "        return a - b\n"
    "    }\n"
    "}\n";

} // namespace functions

} // namespace luma::lsp::test_fixtures

#endif // LUMA_LSP_TEST_FIXTURES_HPP
