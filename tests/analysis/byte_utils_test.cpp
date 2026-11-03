// Unit tests for core/common/byte_utils.hpp.

#include <array>
#include <cstdint>
#include <vector>

#include "common/byte_utils.hpp"
#include "test_framework.hpp"

using luma::read_u16_be;
using luma::read_u32_be;
using luma::read_u8_checked;
using luma::write_u16_be;

namespace {

struct ReadU16Case {
    std::array<std::uint8_t, 2> bytes;
    std::uint16_t expected;
};

struct ReadU32Case {
    std::array<std::uint8_t, 4> bytes;
    std::uint32_t expected;
};

struct WriteU16Case {
    std::uint16_t value;
    std::array<std::uint8_t, 2> expected_bytes;
};

// clang-format off
const std::vector<ReadU16Case> read_u16_be_cases = {
    {.bytes = {0x00, 0x00}, .expected = 0},
    {.bytes = {0x00, 0x01}, .expected = 1},
    {.bytes = {0x01, 0x00}, .expected = 256},    // high byte only
    {.bytes = {0xFF, 0xFF}, .expected = 0xFFFF},
    {.bytes = {0x12, 0x34}, .expected = 0x1234}, // 4660
};

const std::vector<ReadU32Case> read_u32_be_cases = {
    {.bytes = {0x00, 0x00, 0x00, 0x00}, .expected = 0},
    {.bytes = {0x00, 0x00, 0x00, 0x01}, .expected = 1},
    {.bytes = {0xFF, 0xFF, 0xFF, 0xFF}, .expected = 0xFFFFFFFF},
    {.bytes = {0xDE, 0xAD, 0xBE, 0xEF}, .expected = 0xDEADBEEF},
    {.bytes = {0x80, 0x00, 0x00, 0x00}, .expected = 0x80000000}, // high byte only
};

const std::vector<WriteU16Case> write_u16_be_cases = {
    {.value = 0,      .expected_bytes = {0x00, 0x00}},
    {.value = 0xFFFF, .expected_bytes = {0xFF, 0xFF}},
    {.value = 0xABCD, .expected_bytes = {0xAB, 0xCD}},
};
// clang-format on

const std::vector<std::uint16_t> u16_round_trip_values = {0, 1, 255, 256, 0x1234, 0xFFFF};

} // namespace

// ═══════════════════════════════════════════════════════════
// read_u16_be
// ═══════════════════════════════════════════════════════════

static void test_read_u16_be(const ReadU16Case& test_case) {
    ASSERT_EQ(read_u16_be(test_case.bytes.data()), test_case.expected);
}

// ═══════════════════════════════════════════════════════════
// read_u32_be
// ═══════════════════════════════════════════════════════════

static void test_read_u32_be(const ReadU32Case& test_case) {
    ASSERT_EQ(read_u32_be(test_case.bytes.data()), test_case.expected);
}

// ═══════════════════════════════════════════════════════════
// write_u16_be
// ═══════════════════════════════════════════════════════════

static void test_write_u16_be(const WriteU16Case& test_case) {
    std::vector<std::uint8_t> buf;
    write_u16_be(buf, test_case.value);
    ASSERT_EQ(buf.size(), 2U);
    ASSERT_EQ(buf[0], test_case.expected_bytes[0]);
    ASSERT_EQ(buf[1], test_case.expected_bytes[1]);
}

static void test_write_u16_be_appends() {
    std::vector<std::uint8_t> buf = {0x42};
    write_u16_be(buf, 0x0100);
    ASSERT_EQ(buf.size(), 3U);
    ASSERT_EQ(buf[0], static_cast<std::uint8_t>(0x42));
    ASSERT_EQ(buf[1], static_cast<std::uint8_t>(0x01));
    ASSERT_EQ(buf[2], static_cast<std::uint8_t>(0x00));
}

// ═══════════════════════════════════════════════════════════
// Round-trip: write_u16_be → read_u16_be
// ═══════════════════════════════════════════════════════════

static void test_u16_round_trip(std::uint16_t value) {
    std::vector<std::uint8_t> buf;
    write_u16_be(buf, value);
    ASSERT_EQ(read_u16_be(buf.data()), value);
}

// ═══════════════════════════════════════════════════════════
// read_u8_checked
// ═══════════════════════════════════════════════════════════

static void test_read_u8_checked_in_bounds() {
    const std::vector<std::uint8_t> code = {0x10, 0x20, 0x30};
    ASSERT_EQ(read_u8_checked(code, 0), static_cast<std::uint8_t>(0x10));
    ASSERT_EQ(read_u8_checked(code, 1), static_cast<std::uint8_t>(0x20));
    ASSERT_EQ(read_u8_checked(code, 2), static_cast<std::uint8_t>(0x30));
}

static void test_read_u8_checked_out_of_bounds_default() {
    const std::vector<std::uint8_t> code = {0x10};
    ASSERT_EQ(read_u8_checked(code, 1), static_cast<std::uint8_t>(0));
    ASSERT_EQ(read_u8_checked(code, 100), static_cast<std::uint8_t>(0));
}

static void test_read_u8_checked_out_of_bounds_custom_default() {
    const std::vector<std::uint8_t> code = {0x10};
    ASSERT_EQ(read_u8_checked(code, 5, 0xFF), static_cast<std::uint8_t>(0xFF));
}

static void test_read_u8_checked_empty_vector() {
    const std::vector<std::uint8_t> code;
    ASSERT_EQ(read_u8_checked(code, 0), static_cast<std::uint8_t>(0));
    ASSERT_EQ(read_u8_checked(code, 0, 0x42), static_cast<std::uint8_t>(0x42));
}

// ─── main ───

int main() {
    RUN_PARAMETERIZED(test_read_u16_be, read_u16_be_cases);
    RUN_PARAMETERIZED(test_read_u32_be, read_u32_be_cases);

    // write_u16_be
    RUN_PARAMETERIZED(test_write_u16_be, write_u16_be_cases);
    RUN(test_write_u16_be_appends);

    // Round-trip
    RUN_PARAMETERIZED(test_u16_round_trip, u16_round_trip_values);

    // read_u8_checked
    RUN(test_read_u8_checked_in_bounds);
    RUN(test_read_u8_checked_out_of_bounds_default);
    RUN(test_read_u8_checked_out_of_bounds_custom_default);
    RUN(test_read_u8_checked_empty_vector);

    return SUMMARY();
}
