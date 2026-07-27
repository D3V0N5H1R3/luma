#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_color_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                              const ParamShorthands& p) {
    append_specs(specs,
                 {
                     m.fn("contrast_ratio", 2, "(a: Color.Color, b: Color.Color)", R::number_type(),
                          {named::color(), named::color()}),
                     m.fn("darken", 2, "(color: Color.Color, amount: number)", named::color(),
                          {named::color(), p.number}),
                     m.fn("from_cmyk", 1, "(cmyk: Color.Cmyk)", named::color(), {named::cmyk()}),
                     m.fn("from_hex", 1, "(hex: string)", R::result(named::color()), {p.string}),
                     m.fn("from_hsl", 1, "(hsl: Color.Hsl)", named::color(), {named::hsl()}),
                     m.fn("from_hsv", 1, "(hsv: Color.Hsv)", named::color(), {named::hsv()}),
                     m.fn("from_name", 1, "(name: Color.Name)", named::color(),
                          {named::color_name()}),
                     m.fn("gradient", 2, "(angle: number, stops: array<Color.Stop>)",
                          named::gradient(), {p.number, R::array(named::color_stop())}),
                     m.fn("gradient_at", 2, "(g: Color.Gradient, position: number)", named::color(),
                          {named::gradient(), p.number}),
                     m.fn("gradient_to_css", 1, "(g: Color.Gradient)", R::string_type(),
                          {named::gradient()}),
                     m.fn("lighten", 2, "(color: Color.Color, amount: number)", named::color(),
                          {named::color(), p.number}),
                     m.fn("mix", 3, "(a: Color.Color, b: Color.Color, t: number)", named::color(),
                          {named::color(), named::color(), p.number}),
                     m.fn("rgb", 3, "(red: integer, green: integer, blue: integer)",
                          R::result(named::color()), {p.integer, p.integer, p.integer}),
                     m.fn("rgba", 4, "(red: integer, green: integer, blue: integer, alpha: number)",
                          R::result(named::color()), {p.integer, p.integer, p.integer, p.number}),
                     m.fn("rotate_hue", 2, "(color: Color.Color, degrees: number)", named::color(),
                          {named::color(), p.number}),
                     m.fn("stop", 2, "(color: Color.Color, position: number)", named::color_stop(),
                          {named::color(), p.number}),
                     m.fn("to_cmyk", 1, "(color: Color.Color)", named::cmyk(), {named::color()}),
                     m.fn("to_css", 1, "(color: Color.Color)", R::string_type(), {named::color()}),
                     m.fn("to_hex", 1, "(color: Color.Color)", R::string_type(), {named::color()}),
                     m.fn("to_hsl", 1, "(color: Color.Color)", named::hsl(), {named::color()}),
                     m.fn("to_hsv", 1, "(color: Color.Color)", named::hsv(), {named::color()}),
                 });
}

} // namespace luma::stdlib::detail
