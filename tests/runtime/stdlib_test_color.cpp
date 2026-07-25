// Standard library tests: Color module.

#include "stdlib_test_helpers.hpp"

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
    const auto v = eval(R"(Color.from_hex("#0172ad"))");
    ASSERT_RESULT_SUCCESS(v);
    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("red")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("green")->as_integer(), static_cast<std::int64_t>(114));
    ASSERT_EQ(rec->find_field("blue")->as_integer(), static_cast<std::int64_t>(173));

    // #rgb shorthand expands each nibble (f0a → ff00aa).
    const auto s = eval(R"(Color.from_hex("f0a"))");
    ASSERT_RESULT_SUCCESS(s);
    const auto& srec = s.as_result()->owned_inner->as_record();
    ASSERT_EQ(srec->find_field("red")->as_integer(), static_cast<std::int64_t>(255));
    ASSERT_EQ(srec->find_field("green")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(srec->find_field("blue")->as_integer(), static_cast<std::int64_t>(170));

    // #rrggbbaa carries alpha (0x80 → 128/255).
    const auto a = eval(R"(Color.from_hex("#00000080"))");
    ASSERT_RESULT_SUCCESS(a);
    ASSERT_NEAR(a.as_result()->owned_inner->as_record()->find_field("alpha")->as_number(),
                128.0 / 255.0, 1e-9);
}

LUMA_TEST(color_from_hex_invalid_fails) {
    ASSERT_EVAL_FAILURE(R"(Color.from_hex("#0172a"))");  // 5 digits
    ASSERT_EVAL_FAILURE(R"(Color.from_hex("#zzzzzz"))"); // non-hex
    ASSERT_EVAL_FAILURE(R"(Color.from_hex(""))");        // empty
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
    ASSERT_TRUE(env->has("Color.from_hex"));
    ASSERT_TRUE(env->has("Color.to_hex"));
    ASSERT_TRUE(env->has("Color.to_css"));
    ASSERT_TRUE(env->has("Color.lighten"));
    ASSERT_TRUE(env->has("Color.darken"));
    ASSERT_TRUE(env->has("Color.mix"));
    ASSERT_TRUE(env->has("Color.contrast_ratio"));
}

int main() {
    LUMA_RUN_ALL();
}
