// Unit tests for RuntimeError and error payload.

#include <string>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_manager.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── RuntimeError construction ───

static void test_runtime_error_construction() {
    const SourceLocation loc{0, 1, 1};

    const RuntimeError re{"runtime boom", loc};
    ASSERT_EQ(std::string{re.what()}, "runtime boom");
    ASSERT_EQ(re.location().file_id, 0);
    ASSERT_EQ(re.location().line, 1);
    ASSERT_EQ(re.location().column, 1);
}

static void test_runtime_error_default_location() {
    const RuntimeError re{"no location"};
    ASSERT_EQ(re.location().line, 1);
    ASSERT_EQ(re.location().column, 1);
}

static void test_runtime_error_with_hint() {
    const SourceLocation loc{0, 1, 1};

    const RuntimeError no_hint{"oops", loc};
    ASSERT_FALSE(no_hint.hint().has_value());

    const RuntimeError with_hint{"oops", loc, "did you mean '+'?"};
    ASSERT_TRUE(with_hint.hint().has_value());
    ASSERT_EQ(with_hint.hint().value(), "did you mean '+'?");
}

static void test_runtime_error_location() {
    const SourceLocation loc{1, 42, 7};
    const RuntimeError err{"wrong type", loc};

    ASSERT_EQ(err.location().file_id, 1);
    ASSERT_EQ(err.location().line, 42);
    ASSERT_EQ(err.location().column, 7);
}

static void test_runtime_error_payload() {
    const SourceLocation loc{0, 1, 1};
    RuntimeError err{"kaboom", loc};

    ASSERT_FALSE(err.has_error_payload());

    err.set_error_payload(std::any{42});
    ASSERT_TRUE(err.has_error_payload());
    ASSERT_TRUE(err.error_payload().has_value());
}

static void test_runtime_error_typed_payload() {
    const SourceLocation loc{0, 1, 1};
    RuntimeError err{"kaboom", loc};

    err.set_error_payload(std::any{42});
    auto val = err.get_error_payload<int>();
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(*val, 42);

    auto bad = err.get_error_payload<std::string>();
    ASSERT_FALSE(bad.has_value());
}

int main() {
    RUN(test_runtime_error_construction);
    RUN(test_runtime_error_default_location);
    RUN(test_runtime_error_with_hint);
    RUN(test_runtime_error_location);
    RUN(test_runtime_error_payload);
    RUN(test_runtime_error_typed_payload);

    return SUMMARY();
}
