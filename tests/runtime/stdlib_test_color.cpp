// Standard library tests: Color module.

#include "stdlib_test_helpers.hpp"

// --- Color.Name (N06) ---

LUMA_TEST(color_from_name_red) {
    const auto v = eval("Color.from_name(Color.Name.Red)");
    ASSERT_TRUE(v.is_record());

    const auto& rec = v.as_record();
    ASSERT_EQ(rec->type_name, std::string{"Color"});
    ASSERT_EQ(rec->find_field("red")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(rec->find_field("green")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(rec->find_field("blue")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_NEAR(rec->find_field("alpha")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(color_from_name_css_canonical) {
    // CSS-canonical: Green is 0,128,0 (distinct from Lime 0,255,0).
    const auto green = eval("Color.from_name(Color.Name.Green)");
    ASSERT_EQ(green.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(128));

    const auto lime = eval("Color.from_name(Color.Name.Lime)");
    ASSERT_EQ(lime.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));

    const auto orange = eval("Color.to_hex(Color.from_name(Color.Name.Orange))");
    ASSERT_EQ(orange.as_string(), std::string{"#ffa500"});
}

LUMA_TEST(color_rgb) {
    const auto v = eval("Color.rgb(1, 114, 173)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"Color"});
    ASSERT_EQ(rec->find_field("red")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("green")->as_integer(), static_cast<std::int64_t>(114));
    ASSERT_EQ(rec->find_field("blue")->as_integer(), static_cast<std::int64_t>(173));
    ASSERT_NEAR(rec->find_field("alpha")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(color_rgb_out_of_range_fails) {
    ASSERT_EVAL_FAILURE("Color.rgb(256, 0, 0)");
    ASSERT_EVAL_FAILURE("Color.rgb(-1, 0, 0)");
}

LUMA_TEST(color_rgba) {
    const auto v = eval("Color.rgba(10, 20, 30, 0.5)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_NEAR(rec->find_field("alpha")->as_number(), 0.5, 1e-9);

    // Alpha must be within 0–1.
    ASSERT_EVAL_FAILURE("Color.rgba(10, 20, 30, 1.5)");
    ASSERT_EVAL_FAILURE("Color.rgba(10, 20, 30, -0.1)");
}

LUMA_TEST(color_from_hex) {
    // #rrggbb
    const auto v = eval(R"(Color.from_hexadecimal("#0172ad"))");
    ASSERT_RESULT_SUCCESS(v);
    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("red")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("green")->as_integer(), static_cast<std::int64_t>(114));
    ASSERT_EQ(rec->find_field("blue")->as_integer(), static_cast<std::int64_t>(173));

    // #rgb shorthand expands each nibble (f0a → ff00aa).
    const auto s = eval(R"(Color.from_hexadecimal("f0a"))");
    ASSERT_RESULT_SUCCESS(s);
    const auto& srec = s.as_result()->owned_inner->as_record();
    ASSERT_EQ(srec->find_field("red")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(srec->find_field("green")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(srec->find_field("blue")->as_integer(), static_cast<std::int64_t>(170));

    // #rrggbbaa carries alpha (0x80 → 128/255).
    const auto a = eval(R"(Color.from_hexadecimal("#00000080"))");
    ASSERT_RESULT_SUCCESS(a);
    ASSERT_NEAR(a.as_result()->owned_inner->as_record()->find_field("alpha")->as_number(),
                128.0 / 255.0, 1e-9);
}

LUMA_TEST(color_from_hex_invalid_fails) {
    ASSERT_EVAL_FAILURE(R"(Color.from_hexadecimal("#0172a"))");  // 5 digits
    ASSERT_EVAL_FAILURE(R"(Color.from_hexadecimal("#zzzzzz"))"); // non-hex
    ASSERT_EVAL_FAILURE(R"(Color.from_hexadecimal(""))");        // empty
}

LUMA_TEST(color_to_hex) {
    ASSERT_EQ(eval(R"(Color.to_hex(Result.unwrap(Color.rgb(1, 114, 173))))").as_string(),
              std::string{"#0172ad"});

    // A non-opaque colour appends the alpha byte.
    ASSERT_EQ(eval(R"(Color.to_hex(Result.unwrap(Color.rgba(0, 0, 0, 0.5))))").as_string(),
              std::string{"#00000080"});
}

LUMA_TEST(color_to_css) {
    ASSERT_EQ(eval(R"(Color.to_css(Result.unwrap(Color.rgb(1, 114, 173))))").as_string(),
              std::string{"rgb(1, 114, 173)"});
    ASSERT_EQ(eval(R"(Color.to_css(Result.unwrap(Color.rgba(1, 2, 3, 0.5))))").as_string(),
              std::string{"rgba(1, 2, 3, 0.5)"});
}

LUMA_TEST(color_lighten_darken) {
    // Lightening black by 100% yields white; darkening white by 100% yields black.
    ASSERT_EQ(
        eval(R"(Color.to_hex(Color.lighten(Result.unwrap(Color.rgb(0, 0, 0)), 1.0)))").as_string(),
        std::string{"#ffffff"});
    ASSERT_EQ(eval(R"(Color.to_hex(Color.darken(Result.unwrap(Color.rgb(255, 255, 255)), 1.0)))")
                  .as_string(),
              std::string{"#000000"});
}

LUMA_TEST(color_mix) {
    // Mixing black and white at t=0.5 gives mid-grey (128).
    const auto v = eval("Color.mix(Result.unwrap(Color.rgb(0, 0, 0)), "
                        "Result.unwrap(Color.rgb(255, 255, 255)), 0.5)");
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(128));
}

LUMA_TEST(color_contrast_ratio) {
    // Black on white is the maximum 21:1 contrast.
    const auto v = eval("Color.contrast_ratio(Result.unwrap(Color.rgb(0, 0, 0)), "
                        "Result.unwrap(Color.rgb(255, 255, 255)))");
    ASSERT_NEAR(v.as_number(), 21.0, 1e-6);

    // A colour against itself is 1:1.
    const auto same = eval("Color.contrast_ratio(Result.unwrap(Color.rgb(120, 120, 120)), "
                           "Result.unwrap(Color.rgb(120, 120, 120)))");
    ASSERT_NEAR(same.as_number(), 1.0, 1e-9);
}

LUMA_TEST(color_module) {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Color.rgb"));
    ASSERT_TRUE(env->has("Color.rgba"));
    ASSERT_TRUE(env->has("Color.from_hexadecimal"));
    ASSERT_TRUE(env->has("Color.to_hex"));
    ASSERT_TRUE(env->has("Color.to_css"));
    ASSERT_TRUE(env->has("Color.lighten"));
    ASSERT_TRUE(env->has("Color.darken"));
    ASSERT_TRUE(env->has("Color.mix"));
    ASSERT_TRUE(env->has("Color.contrast_ratio"));
    ASSERT_TRUE(env->has("Color.to_hsl"));
    ASSERT_TRUE(env->has("Color.from_hsl"));
    ASSERT_TRUE(env->has("Color.rotate_hue"));
    ASSERT_TRUE(env->has("Color.to_cmyk"));
    ASSERT_TRUE(env->has("Color.from_cmyk"));
}

// ─── Color.Hsl: HSL conversions ──────────────────────────────────────────────

LUMA_TEST(color_to_hsl_red) {
    // Pure red is hue 0, full saturation, half lightness.
    const auto v = eval("Color.to_hsl(Result.unwrap(Color.rgb(255, 0, 0)))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Hsl"});
    ASSERT_NEAR(v.as_record()->find_field("hue")->as_number(), 0.0, 0.5);
    ASSERT_NEAR(v.as_record()->find_field("saturation")->as_number(), 1.0, 1e-6);
    ASSERT_NEAR(v.as_record()->find_field("lightness")->as_number(), 0.5, 1e-6);
}

LUMA_TEST(color_from_hsl_roundtrip) {
    // Green: hue 120, full saturation, half lightness -> (0, 255, 0).
    const auto v = eval("Color.from_hsl(Color.to_hsl(Result.unwrap(Color.rgb(0, 255, 0))))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Color"});
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("blue")->as_integer(), static_cast<std::int64_t>(0));
}

LUMA_TEST(color_rotate_hue_preserves_alpha) {
    // Rotating red by 120 degrees yields green; the original alpha is kept.
    const auto v = eval("Color.rotate_hue(Result.unwrap(Color.rgba(255, 0, 0, 0.5)), 120.0)");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_NEAR(v.as_record()->find_field("alpha")->as_number(), 0.5, 1e-9);
}

// ─── Color.Hsv: HSV (HSB) conversions ────────────────────────────────────────

LUMA_TEST(color_to_hsv_red) {
    // Pure red is hue 0, full saturation, full value.
    const auto v = eval("Color.to_hsv(Result.unwrap(Color.rgb(255, 0, 0)))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Hsv"});
    ASSERT_NEAR(v.as_record()->find_field("hue")->as_number(), 0.0, 0.5);
    ASSERT_NEAR(v.as_record()->find_field("saturation")->as_number(), 1.0, 1e-6);
    ASSERT_NEAR(v.as_record()->find_field("value")->as_number(), 1.0, 1e-6);
}

LUMA_TEST(color_from_hsv_roundtrip) {
    // Green: hue 120, full saturation, full value -> (0, 255, 0).
    const auto v = eval("Color.from_hsv(Color.to_hsv(Result.unwrap(Color.rgb(0, 255, 0))))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Color"});
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("blue")->as_integer(), static_cast<std::int64_t>(0));
}

// ─── Color.Cmyk: CMYK conversions ────────────────────────────────────────────

LUMA_TEST(color_to_cmyk_black) {
    // Pure black is 0 cyan/magenta/yellow, full key.
    const auto v = eval("Color.to_cmyk(Result.unwrap(Color.rgb(0, 0, 0)))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Cmyk"});
    ASSERT_NEAR(v.as_record()->find_field("cyan")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("magenta")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("yellow")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("key")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(color_to_cmyk_red) {
    // Pure red has no cyan or key, and is fully magenta and yellow.
    const auto v = eval("Color.to_cmyk(Result.unwrap(Color.rgb(255, 0, 0)))");

    ASSERT_NEAR(v.as_record()->find_field("cyan")->as_number(), 0.0, 1e-6);
    ASSERT_NEAR(v.as_record()->find_field("magenta")->as_number(), 1.0, 1e-6);
    ASSERT_NEAR(v.as_record()->find_field("yellow")->as_number(), 1.0, 1e-6);
    ASSERT_NEAR(v.as_record()->find_field("key")->as_number(), 0.0, 1e-6);
}

LUMA_TEST(color_from_cmyk_roundtrip) {
    // Green round-trips through CMYK.
    const auto v = eval("Color.from_cmyk(Color.to_cmyk(Result.unwrap(Color.rgb(0, 255, 0))))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Color"});
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("blue")->as_integer(), static_cast<std::int64_t>(0));
}

// ─── Analysis / accessibility helpers ────────────────────────────────────────

LUMA_TEST(color_luminance) {
    // Pure white has a WCAG relative luminance of 1, black of 0.
    ASSERT_NEAR(eval("Color.luminance(Result.unwrap(Color.rgb(255, 255, 255)))").as_number(), 1.0,
                0.01);
    ASSERT_NEAR(eval("Color.luminance(Result.unwrap(Color.rgb(0, 0, 0)))").as_number(), 0.0, 0.01);
}

LUMA_TEST(color_brightness) {
    // Perceived brightness of white is 1, black is 0.
    ASSERT_NEAR(eval("Color.brightness(Result.unwrap(Color.rgb(255, 255, 255)))").as_number(), 1.0,
                0.01);
    ASSERT_NEAR(eval("Color.brightness(Result.unwrap(Color.rgb(0, 0, 0)))").as_number(), 0.0, 0.01);
}

LUMA_TEST(color_is_light_is_dark) {
    ASSERT_TRUE(eval("Color.is_light(Result.unwrap(Color.rgb(255, 255, 255)))").as_bool());
    ASSERT_FALSE(eval("Color.is_dark(Result.unwrap(Color.rgb(255, 255, 255)))").as_bool());

    ASSERT_FALSE(eval("Color.is_light(Result.unwrap(Color.rgb(0, 0, 0)))").as_bool());
    ASSERT_TRUE(eval("Color.is_dark(Result.unwrap(Color.rgb(0, 0, 0)))").as_bool());
}

LUMA_TEST(color_readable_text_color) {
    // White background reads best with black text; black background with white.
    const auto on_white =
        eval("Color.readable_text_color(Result.unwrap(Color.rgb(255, 255, 255)))");
    ASSERT_EQ(on_white.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));

    const auto on_black = eval("Color.readable_text_color(Result.unwrap(Color.rgb(0, 0, 0)))");
    ASSERT_EQ(on_black.as_record()->find_field("red")->as_integer(),
              static_cast<std::int64_t>(255));
}

// ─── Saturation adjustments ──────────────────────────────────────────────────

LUMA_TEST(color_saturate_desaturate) {
    // Desaturating a fully saturated colour reduces its HSL saturation.
    const auto v = eval("Color.to_hsl(Color.desaturate(Result.unwrap(Color.rgb(255, 0, 0)), 0.5))");
    ASSERT_NEAR(v.as_record()->find_field("saturation")->as_number(), 0.5, 0.01);

    // Saturate clamps to 1.0; a mid colour saturated fully reaches full saturation.
    const auto s =
        eval("Color.to_hsl(Color.saturate(Result.unwrap(Color.rgb(150, 100, 100)), 1.0))");
    ASSERT_NEAR(s.as_record()->find_field("saturation")->as_number(), 1.0, 0.01);
}

LUMA_TEST(color_grayscale) {
    // Grayscale drops saturation to zero, leaving a neutral grey (r == g == b).
    const auto v = eval("Color.grayscale(Result.unwrap(Color.rgb(255, 0, 0)))");
    const auto r = v.as_record()->find_field("red")->as_integer();
    const auto g = v.as_record()->find_field("green")->as_integer();
    const auto b = v.as_record()->find_field("blue")->as_integer();
    ASSERT_EQ(r, g);
    ASSERT_EQ(g, b);
}

// ─── Alpha adjustments ───────────────────────────────────────────────────────

LUMA_TEST(color_with_alpha) {
    const auto v = eval("Color.with_alpha(Result.unwrap(Color.rgb(10, 20, 30)), 0.25)");
    ASSERT_NEAR(v.as_record()->find_field("alpha")->as_number(), 0.25, 1e-9);
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(10));
}

LUMA_TEST(color_fade) {
    // Fading reduces alpha, clamping at zero.
    const auto v = eval("Color.fade(Result.unwrap(Color.rgba(10, 20, 30, 0.8)), 0.3)");
    ASSERT_NEAR(v.as_record()->find_field("alpha")->as_number(), 0.5, 1e-9);

    const auto floored = eval("Color.fade(Result.unwrap(Color.rgba(10, 20, 30, 0.2)), 0.5)");
    ASSERT_NEAR(floored.as_record()->find_field("alpha")->as_number(), 0.0, 1e-9);
}

// ─── Per-channel and hue-based derivations ───────────────────────────────────

LUMA_TEST(color_invert) {
    // Inversion flips each channel and preserves alpha.
    const auto v = eval("Color.invert(Result.unwrap(Color.rgba(255, 0, 128, 0.5)))");
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("blue")->as_integer(), static_cast<std::int64_t>(127));
    ASSERT_NEAR(v.as_record()->find_field("alpha")->as_number(), 0.5, 1e-9);
}

LUMA_TEST(color_complement) {
    // The complement of red (hue 0) is cyan (hue 180): (0, 255, 255).
    const auto v = eval("Color.complement(Result.unwrap(Color.rgb(255, 0, 0)))");
    ASSERT_EQ(v.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(v.as_record()->find_field("green")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(v.as_record()->find_field("blue")->as_integer(), static_cast<std::int64_t>(255));
}

LUMA_TEST(color_complementary) {
    const auto v = eval("Color.complementary(Result.unwrap(Color.rgb(255, 0, 0)))");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), static_cast<std::size_t>(2));

    const auto& base = (*v.as_array()->elements)[0];
    ASSERT_EQ(base.as_record()->find_field("red")->as_integer(), static_cast<std::int64_t>(255));

    const auto& complement = (*v.as_array()->elements)[1];
    ASSERT_EQ(complement.as_record()->find_field("green")->as_integer(),
              static_cast<std::int64_t>(255));
}

LUMA_TEST(color_triadic) {
    // Red's triad is red, green (+120), blue (+240).
    const auto v = eval("Color.triadic(Result.unwrap(Color.rgb(255, 0, 0)))");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), static_cast<std::size_t>(3));

    ASSERT_EQ((*v.as_array()->elements)[0].as_record()->find_field("red")->as_integer(),
              static_cast<std::int64_t>(255));
    ASSERT_EQ((*v.as_array()->elements)[1].as_record()->find_field("green")->as_integer(),
              static_cast<std::int64_t>(255));
    ASSERT_EQ((*v.as_array()->elements)[2].as_record()->find_field("blue")->as_integer(),
              static_cast<std::int64_t>(255));
}

LUMA_TEST(color_analogous) {
    const auto v = eval("Color.analogous(Result.unwrap(Color.rgb(255, 0, 0)))");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), static_cast<std::size_t>(3));

    // The base is unchanged; the neighbours are ±30° in hue.
    ASSERT_EQ((*v.as_array()->elements)[0].as_record()->find_field("red")->as_integer(),
              static_cast<std::int64_t>(255));
}

LUMA_TEST(color_new_module_registration) {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Color.luminance"));
    ASSERT_TRUE(env->has("Color.brightness"));
    ASSERT_TRUE(env->has("Color.is_light"));
    ASSERT_TRUE(env->has("Color.is_dark"));
    ASSERT_TRUE(env->has("Color.readable_text_color"));
    ASSERT_TRUE(env->has("Color.saturate"));
    ASSERT_TRUE(env->has("Color.desaturate"));
    ASSERT_TRUE(env->has("Color.grayscale"));
    ASSERT_TRUE(env->has("Color.with_alpha"));
    ASSERT_TRUE(env->has("Color.fade"));
    ASSERT_TRUE(env->has("Color.invert"));
    ASSERT_TRUE(env->has("Color.complement"));
    ASSERT_TRUE(env->has("Color.complementary"));
    ASSERT_TRUE(env->has("Color.triadic"));
    ASSERT_TRUE(env->has("Color.analogous"));
}

int main() {
    LUMA_RUN_ALL();
}
