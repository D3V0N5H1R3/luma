// color_module.cpp — the Color module: a typed RGBA colour value.
//
// A Color record { red, green, blue, alpha } (channels 0–255, alpha 0–1) with
// validating constructors and derivations.  Every value serialises to a CSS
// string the GraphicalUi web-view already accepts, so Solaris themes can be
// computed rather than hand-written.  Data + free functions, no operator
// overloading — the same philosophy as Decimal and Math.Fraction.

#include "runtime/stdlib/io/color_module.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/format_number.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// An RGBA colour: 8-bit channels plus a 0–1 alpha.
struct Rgba {
    int red;
    int green;
    int blue;
    double alpha;
};

// Round a 0–1 unit value to an 0–255 integer channel.
[[nodiscard]] int to_channel(double unit) {
    return static_cast<int>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
}

// Build a Color record value.  The short runtime type_name "Color" matches the
// "Color.Color" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_color(const Rgba& c) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Color";
    rec->fields.emplace_back("red", Value{static_cast<std::int64_t>(c.red)});
    rec->fields.emplace_back("green", Value{static_cast<std::int64_t>(c.green)});
    rec->fields.emplace_back("blue", Value{static_cast<std::int64_t>(c.blue)});
    rec->fields.emplace_back("alpha", Value{c.alpha});

    return Value{std::move(rec)};
}

// Read a Color record argument.  Throws a RuntimeError when the value is not a
// colour-shaped record.  Channels are clamped to 0–255 and alpha to 0–1 so a
// hand-built record can never carry out-of-range data into a derivation.
[[nodiscard]] Rgba read_color(const Value& value, std::string_view func,
                              const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Color.Color record", loc,
                           "build one with Color.rgb(red, green, blue)"};
    }

    const auto& rec = value.as_record();
    const Value* r = rec->find_field("red");
    const Value* g = rec->find_field("green");
    const Value* b = rec->find_field("blue");
    const Value* a = rec->find_field("alpha");

    if (r == nullptr || !r->is_integer() || g == nullptr || !g->is_integer() || b == nullptr ||
        !b->is_integer() || a == nullptr || !(a->is_integer() || a->is_number())) {
        throw RuntimeError{std::string{func} + ": expected a Color.Color record", loc,
                           "build one with Color.rgb(red, green, blue)"};
    }

    return Rgba{static_cast<int>(std::clamp<std::int64_t>(r->as_integer(), 0, 255)),
                static_cast<int>(std::clamp<std::int64_t>(g->as_integer(), 0, 255)),
                static_cast<int>(std::clamp<std::int64_t>(b->as_integer(), 0, 255)),
                std::clamp(a->to_numeric(), 0.0, 1.0)};
}

// Validate a raw integer channel is within 0–255.
[[nodiscard]] std::optional<Value> check_channel(std::int64_t v, std::string_view func,
                                                 std::string_view name) {
    if (v < 0 || v > 255) {
        return make_failure_value(error_msg(
            "Color", func, std::format("{} channel {} is out of range (0–255)", name, v)));
    }

    return std::nullopt;
}

// Parse a single hex digit; nullopt when the character is not [0-9a-fA-F].
[[nodiscard]] std::optional<int> hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return std::nullopt;
}

// Parse two hex digits into a 0–255 byte; nullopt when either digit is invalid.
[[nodiscard]] std::optional<int> hex_byte(char high, char low) {
    const auto h = hex_digit(high);
    const auto l = hex_digit(low);

    if (!h || !l) {
        return std::nullopt;
    }

    return (*h << 4) | *l;
}

// Convert a channel value to a two-digit lowercase hex string.
[[nodiscard]] std::string channel_to_hex(int v) {
    return std::format("{:02x}", v);
}

// WCAG relative luminance of a colour (alpha ignored, opaque assumption).
[[nodiscard]] double relative_luminance(const Rgba& c) {
    const auto linear = [](int channel) {
        const double cs = static_cast<double>(channel) / 255.0;
        return cs <= 0.03928 ? cs / 12.92 : std::pow((cs + 0.055) / 1.055, 2.4);
    };

    return (0.2126 * linear(c.red)) + (0.7152 * linear(c.green)) + (0.0722 * linear(c.blue));
}

// Linear interpolation between two integer channels at t in [0, 1].
[[nodiscard]] int lerp_channel(int from, int to, double t) {
    return to_channel(((static_cast<double>(from) / 255.0) * (1.0 - t)) +
                      ((static_cast<double>(to) / 255.0) * t));
}

// A colour in the hue/saturation/lightness cylinder: hue in degrees [0, 360),
// saturation and lightness as 0–1 ratios.
struct Hsl {
    double hue;
    double saturation;
    double lightness;
};

// Build a Color.Hsl record value.  The short runtime type_name "Hsl" matches the
// "Color.Hsl" record registered in stdlib_type_arities.cpp.  Hue is an angle and
// saturation/lightness are ratios, so every field is a `number`.
[[nodiscard]] Value make_hsl(const Hsl& c) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Hsl";
    rec->fields.emplace_back("hue", Value{c.hue});
    rec->fields.emplace_back("saturation", Value{c.saturation});
    rec->fields.emplace_back("lightness", Value{c.lightness});

    return Value{std::move(rec)};
}

// Read a Color.Hsl record argument, wrapping the hue into [0, 360) and clamping
// saturation/lightness to 0–1 so a hand-built record can never carry out-of-range
// data into a conversion.  Throws when the value is not an HSL-shaped record.
[[nodiscard]] Hsl read_hsl(const Value& value, std::string_view func, const SourceLocation& loc) {
    const auto invalid = [&] {
        throw RuntimeError{std::string{func} + ": expected a Color.Hsl record", loc,
                           "build one with Color.to_hsl(color)"};
    };

    if (!value.is_record()) {
        invalid();
    }

    const auto& rec = value.as_record();
    const Value* h = rec->find_field("hue");
    const Value* s = rec->find_field("saturation");
    const Value* l = rec->find_field("lightness");

    const auto numeric = [](const Value* v) {
        return v != nullptr && (v->is_integer() || v->is_number());
    };

    if (!numeric(h) || !numeric(s) || !numeric(l)) {
        invalid();
    }

    double hue = std::fmod(h->to_numeric(), 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }

    return Hsl{hue, std::clamp(s->to_numeric(), 0.0, 1.0), std::clamp(l->to_numeric(), 0.0, 1.0)};
}

// Convert an RGBA colour to HSL (alpha dropped — HSL has no alpha channel).
[[nodiscard]] Hsl rgb_to_hsl(const Rgba& c) {
    const double r = static_cast<double>(c.red) / 255.0;
    const double g = static_cast<double>(c.green) / 255.0;
    const double b = static_cast<double>(c.blue) / 255.0;

    const double max = std::max({r, g, b});
    const double min = std::min({r, g, b});
    const double delta = max - min;
    const double lightness = (max + min) / 2.0;

    double hue = 0.0;
    double saturation = 0.0;

    if (delta > 0.0) {
        // delta > 0 implies the colour is neither pure black nor white, so the
        // denominator (1 - |2L - 1|) is strictly positive.
        saturation = delta / (1.0 - std::fabs((2.0 * lightness) - 1.0));

        if (max == r) {
            hue = 60.0 * std::fmod((g - b) / delta, 6.0);
        } else if (max == g) {
            hue = 60.0 * (((b - r) / delta) + 2.0);
        } else {
            hue = 60.0 * (((r - g) / delta) + 4.0);
        }

        if (hue < 0.0) {
            hue += 360.0;
        }
    }

    return Hsl{hue, std::clamp(saturation, 0.0, 1.0), std::clamp(lightness, 0.0, 1.0)};
}

// Convert an HSL colour back to RGBA, attaching the supplied alpha.
[[nodiscard]] Rgba hsl_to_rgb(const Hsl& h, double alpha) {
    const double chroma = (1.0 - std::fabs((2.0 * h.lightness) - 1.0)) * h.saturation;
    const double hp = h.hue / 60.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    const double m = h.lightness - (chroma / 2.0);

    double r1 = 0.0;
    double g1 = 0.0;
    double b1 = 0.0;

    if (hp < 1.0) {
        r1 = chroma;
        g1 = x;
    } else if (hp < 2.0) {
        r1 = x;
        g1 = chroma;
    } else if (hp < 3.0) {
        g1 = chroma;
        b1 = x;
    } else if (hp < 4.0) {
        g1 = x;
        b1 = chroma;
    } else if (hp < 5.0) {
        r1 = x;
        b1 = chroma;
    } else {
        r1 = chroma;
        b1 = x;
    }

    return Rgba{to_channel(r1 + m), to_channel(g1 + m), to_channel(b1 + m),
                std::clamp(alpha, 0.0, 1.0)};
}

// A colour in the hue/saturation/value cylinder (HSB): hue in degrees [0, 360),
// saturation and value as 0–1 ratios.
struct Hsv {
    double hue;
    double saturation;
    double value;
};

// Build a Color.Hsv record value.  The short runtime type_name "Hsv" matches the
// "Color.Hsv" record registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_hsv(const Hsv& c) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Hsv";
    rec->fields.emplace_back("hue", Value{c.hue});
    rec->fields.emplace_back("saturation", Value{c.saturation});
    rec->fields.emplace_back("value", Value{c.value});

    return Value{std::move(rec)};
}

// Read a Color.Hsv record argument, wrapping the hue into [0, 360) and clamping
// saturation/value to 0–1 so a hand-built record can never carry out-of-range
// data into a conversion.  Throws when the value is not an HSV-shaped record.
[[nodiscard]] Hsv read_hsv(const Value& value, std::string_view func, const SourceLocation& loc) {
    const auto invalid = [&] {
        throw RuntimeError{std::string{func} + ": expected a Color.Hsv record", loc,
                           "build one with Color.to_hsv(color)"};
    };

    if (!value.is_record()) {
        invalid();
    }

    const auto& rec = value.as_record();
    const Value* h = rec->find_field("hue");
    const Value* s = rec->find_field("saturation");
    const Value* v = rec->find_field("value");

    const auto numeric = [](const Value* val) {
        return val != nullptr && (val->is_integer() || val->is_number());
    };

    if (!numeric(h) || !numeric(s) || !numeric(v)) {
        invalid();
    }

    double hue = std::fmod(h->to_numeric(), 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }

    return Hsv{hue, std::clamp(s->to_numeric(), 0.0, 1.0), std::clamp(v->to_numeric(), 0.0, 1.0)};
}

// Convert an RGBA colour to HSV (alpha dropped — HSV has no alpha channel).
[[nodiscard]] Hsv rgb_to_hsv(const Rgba& c) {
    const double r = static_cast<double>(c.red) / 255.0;
    const double g = static_cast<double>(c.green) / 255.0;
    const double b = static_cast<double>(c.blue) / 255.0;

    const double max = std::max({r, g, b});
    const double min = std::min({r, g, b});
    const double delta = max - min;

    double hue = 0.0;

    if (delta > 0.0) {
        if (max == r) {
            hue = 60.0 * std::fmod((g - b) / delta, 6.0);
        } else if (max == g) {
            hue = 60.0 * (((b - r) / delta) + 2.0);
        } else {
            hue = 60.0 * (((r - g) / delta) + 4.0);
        }

        if (hue < 0.0) {
            hue += 360.0;
        }
    }

    // Value is the max channel; saturation is delta relative to that max.
    const double saturation = (max > 0.0) ? (delta / max) : 0.0;

    return Hsv{hue, std::clamp(saturation, 0.0, 1.0), std::clamp(max, 0.0, 1.0)};
}

// Convert an HSV colour back to RGBA, attaching the supplied alpha.
[[nodiscard]] Rgba hsv_to_rgb(const Hsv& h, double alpha) {
    const double chroma = h.value * h.saturation;
    const double hp = h.hue / 60.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    const double m = h.value - chroma;

    double r1 = 0.0;
    double g1 = 0.0;
    double b1 = 0.0;

    if (hp < 1.0) {
        r1 = chroma;
        g1 = x;
    } else if (hp < 2.0) {
        r1 = x;
        g1 = chroma;
    } else if (hp < 3.0) {
        g1 = chroma;
        b1 = x;
    } else if (hp < 4.0) {
        g1 = x;
        b1 = chroma;
    } else if (hp < 5.0) {
        r1 = x;
        b1 = chroma;
    } else {
        r1 = chroma;
        b1 = x;
    }

    return Rgba{to_channel(r1 + m), to_channel(g1 + m), to_channel(b1 + m),
                std::clamp(alpha, 0.0, 1.0)};
}

} // namespace

void register_color_ns(const EnvPtr& env) {
    ModuleBuilder{"Color", env}
        .func("rgb", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto r = expect_integer(args[0], "Color.rgb", loc);
            const auto g = expect_integer(args[1], "Color.rgb", loc);
            const auto b = expect_integer(args[2], "Color.rgb", loc);

            if (auto fail = check_channel(r, "rgb", "red")) {
                return *std::move(fail);
            }
            if (auto fail = check_channel(g, "rgb", "green")) {
                return *std::move(fail);
            }
            if (auto fail = check_channel(b, "rgb", "blue")) {
                return *std::move(fail);
            }

            return make_success_value(make_color(
                Rgba{static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 1.0}));
        })
        .func("rgba", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto r = expect_integer(args[0], "Color.rgba", loc);
            const auto g = expect_integer(args[1], "Color.rgba", loc);
            const auto b = expect_integer(args[2], "Color.rgba", loc);
            const auto a = expect_numeric(args[3], "Color.rgba", loc);

            if (auto fail = check_channel(r, "rgba", "red")) {
                return *std::move(fail);
            }
            if (auto fail = check_channel(g, "rgba", "green")) {
                return *std::move(fail);
            }
            if (auto fail = check_channel(b, "rgba", "blue")) {
                return *std::move(fail);
            }
            if (a < 0.0 || a > 1.0) {
                return make_failure_value(
                    error_msg("Color", "rgba", "alpha must be in the range 0–1"));
            }

            return make_success_value(
                make_color(Rgba{static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), a}));
        })
        .func("from_hex", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            std::string_view hex = expect_string(args[0], "Color.from_hex", loc);

            if (!hex.empty() && hex.front() == '#') {
                hex.remove_prefix(1);
            }

            const auto invalid = [] {
                return make_failure_value(
                    error_msg("Color", "from_hex", "expected #rgb, #rgba, #rrggbb, or #rrggbbaa"));
            };

            // Expand a 3/4-digit shorthand (#rgb / #rgba) to its full form by
            // doubling each nibble, then parse #rrggbb / #rrggbbaa.
            std::string full;
            if (hex.size() == 3 || hex.size() == 4) {
                for (const char c : hex) {
                    full += c;
                    full += c;
                }
            } else if (hex.size() == 6 || hex.size() == 8) {
                full = std::string{hex};
            } else {
                return invalid();
            }

            const auto r = hex_byte(full[0], full[1]);
            const auto g = hex_byte(full[2], full[3]);
            const auto b = hex_byte(full[4], full[5]);

            if (!r || !g || !b) {
                return invalid();
            }

            double alpha = 1.0;
            if (full.size() == 8) {
                const auto a = hex_byte(full[6], full[7]);
                if (!a) {
                    return invalid();
                }
                alpha = static_cast<double>(*a) / 255.0;
            }

            return make_success_value(make_color(Rgba{*r, *g, *b, alpha}));
        })
        .func("to_hex", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.to_hex", loc);

            std::string out =
                "#" + channel_to_hex(c.red) + channel_to_hex(c.green) + channel_to_hex(c.blue);

            // Append the alpha byte only when the colour is not fully opaque, so
            // opaque colours stay in the familiar #rrggbb form.
            if (c.alpha < 1.0) {
                out += channel_to_hex(to_channel(c.alpha));
            }

            return Value{std::move(out)};
        })
        .func("to_css", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.to_css", loc);

            if (c.alpha >= 1.0) {
                return Value{std::format("rgb({}, {}, {})", c.red, c.green, c.blue)};
            }

            return Value{std::format("rgba({}, {}, {}, {})", c.red, c.green, c.blue,
                                     format_number(c.alpha))};
        })
        .func("lighten", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.lighten", loc);
            const double amount =
                std::clamp(expect_numeric(args[1], "Color.lighten", loc), 0.0, 1.0);

            // Blend toward white by `amount`, keeping the original alpha.
            return make_color(Rgba{lerp_channel(c.red, 255, amount),
                                   lerp_channel(c.green, 255, amount),
                                   lerp_channel(c.blue, 255, amount), c.alpha});
        })
        .func("darken", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.darken", loc);
            const double amount =
                std::clamp(expect_numeric(args[1], "Color.darken", loc), 0.0, 1.0);

            // Blend toward black by `amount`, keeping the original alpha.
            return make_color(Rgba{lerp_channel(c.red, 0, amount), lerp_channel(c.green, 0, amount),
                                   lerp_channel(c.blue, 0, amount), c.alpha});
        })
        .func("mix", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_color(args[0], "Color.mix", loc);
            const auto b = read_color(args[1], "Color.mix", loc);
            const double t = std::clamp(expect_numeric(args[2], "Color.mix", loc), 0.0, 1.0);

            return make_color(Rgba{lerp_channel(a.red, b.red, t), lerp_channel(a.green, b.green, t),
                                   lerp_channel(a.blue, b.blue, t),
                                   (a.alpha * (1.0 - t)) + (b.alpha * t)});
        })
        .func("contrast_ratio", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_color(args[0], "Color.contrast_ratio", loc);
            const auto b = read_color(args[1], "Color.contrast_ratio", loc);

            const double la = relative_luminance(a);
            const double lb = relative_luminance(b);

            const double lighter = std::max(la, lb);
            const double darker = std::min(la, lb);

            // WCAG 2.x contrast ratio, from 1:1 (identical) to 21:1 (black/white).
            return Value{(lighter + 0.05) / (darker + 0.05)};
        })
        .func("to_hsl", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.to_hsl", loc);

            return make_hsl(rgb_to_hsl(c));
        })
        .func("from_hsl", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto h = read_hsl(args[0], "Color.from_hsl", loc);

            return make_color(hsl_to_rgb(h, 1.0));
        })
        .func("to_hsv", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.to_hsv", loc);

            return make_hsv(rgb_to_hsv(c));
        })
        .func("from_hsv", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto h = read_hsv(args[0], "Color.from_hsv", loc);

            return make_color(hsv_to_rgb(h, 1.0));
        })
        .func("rotate_hue", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_color(args[0], "Color.rotate_hue", loc);
            const double degrees = expect_numeric(args[1], "Color.rotate_hue", loc);

            auto hsl = rgb_to_hsl(c);
            hsl.hue = std::fmod(hsl.hue + degrees, 360.0);
            if (hsl.hue < 0.0) {
                hsl.hue += 360.0;
            }

            // Preserve the original alpha through the round-trip (HSL drops it).
            return make_color(hsl_to_rgb(hsl, c.alpha));
        });
}

} // namespace luma
