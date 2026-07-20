// Unit tests for core/common/byte_utils.hpp.

#include <cstdint>
#include <vector>

#include "common/byte_utils.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// read_u16_be
// ═══════════════════════════════════════════════════════════

static void test_read_u16_be_zero() {
    const std::uint8_t data[] = {0x00, 0x00};
    ASSERT_EQ(read_u16_be(data), static_cast<std::uint16_t>(0));
}

static void test_read_u16_be_one() {
    const std::uint8_t data[] = {0x00, 0x01};
    ASSERT_EQ(read_u16_be(data), static_cast<std::uint16_t>(1));
}

static void test_read_u16_be_high_byte_only() {
    const std::uint8_t data[] = {0x01, 0x00};
    ASSERT_EQ(read_u16_be(data), static_cast<std::uint16_t>(256));
}

static void test_read_u16_be_max() {
    const std::uint8_t data[] = {0xFF, 0xFF};
    ASSERT_EQ(read_u16_be(data), static_cast<std::uint16_t>(0xFFFF));
}

static void test_read_u16_be_known_value() {
    // 0x1234 = 4660
    const std::uint8_t data[] = {0x12, 0x34};
    ASSERT_EQ(read_u16_be(data), static_cast<std::uint16_t>(0x1234));
}

// ═══════════════════════════════════════════════════════════
// read_u32_be
// ═══════════════════════════════════════════════════════════

static void test_read_u32_be_zero() {
    const std::uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    ASSERT_EQ(read_u32_be(data), static_cast<std::uint32_t>(0));
}

static void test_read_u32_be_one() {
    const std::uint8_t data[] = {0x00, 0x00, 0x00, 0x01};
    ASSERT_EQ(read_u32_be(data), static_cast<std::uint32_t>(1));
}

static void test_read_u32_be_max() {
    const std::uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    ASSERT_EQ(read_u32_be(data), static_cast<std::uint32_t>(0xFFFFFFFF));
}

static void test_read_u32_be_known_value() {
    // 0xDEADBEEF
    const std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_EQ(read_u32_be(data), static_cast<std::uint32_t>(0xDEADBEEF));
}

static void test_read_u32_be_high_byte_only() {
    const std::uint8_t data[] = {0x80, 0x00, 0x00, 0x00};
    ASSERT_EQ(read_u32_be(data), static_cast<std::uint32_t>(0x80000000));
}

// ═══════════════════════════════════════════════════════════
// write_u16_be
// ═══════════════════════════════════════════════════════════

static void test_write_u16_be_zero() {
    std::vector<std::uint8_t> buf;
    write_u16_be(buf, 0);
    ASSERT_EQ(buf.size(), 2U);
    ASSERT_EQ(buf[0], static_cast<std::uint8_t>(0x00));
    ASSERT_EQ(buf[1], static_cast<std::uint8_t>(0x00));
}

static void test_write_u16_be_max() {
    std::vector<std::uint8_t> buf;
    write_u16_be(buf, 0xFFFF);
    ASSERT_EQ(buf.size(), 2U);
    ASSERT_EQ(buf[0], static_cast<std::uint8_t>(0xFF));
    ASSERT_EQ(buf[1], static_cast<std::uint8_t>(0xFF));
}

static void test_write_u16_be_known_value() {
    std::vector<std::uint8_t> buf;
    write_u16_be(buf, 0xABCD);
    ASSERT_EQ(buf.size(), 2U);
    ASSERT_EQ(buf[0], static_cast<std::uint8_t>(0xAB));
    ASSERT_EQ(buf[1], static_cast<std::uint8_t>(0xCD));
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

static void test_u16_round_trip() {
    const std::uint16_t values[] = {0, 1, 255, 256, 0x1234, 0xFFFF};
    for (auto val : values) {
        std::vector<std::uint8_t> buf;
        write_u16_be(buf, val);
        ASSERT_EQ(read_u16_be(buf.data()), val);
    }
}

// ═══════════════════════════════════════════════════════════
// read_u8_checked
// ═══════════════════════════════════════════════════════════

static void test_read_u8_checked_in_bounds() {
    std::vector<std::uint8_t> code = {0x10, 0x20, 0x30};
    ASSERT_EQ(read_u8_checked(code, 0), static_cast<std::uint8_t>(0x10));
    ASSERT_EQ(read_u8_checked(code, 1), static_cast<std::uint8_t>(0x20));
    ASSERT_EQ(read_u8_checked(code, 2), static_cast<std::uint8_t>(0x30));
}

static void test_read_u8_checked_out_of_bounds_default() {
    std::vector<std::uint8_t> code = {0x10};
    ASSERT_EQ(read_u8_checked(code, 1), static_cast<std::uint8_t>(0));
    ASSERT_EQ(read_u8_checked(code, 100), static_cast<std::uint8_t>(0));
}

static void test_read_u8_checked_out_of_bounds_custom_default() {
    std::vector<std::uint8_t> code = {0x10};
    ASSERT_EQ(read_u8_checked(code, 5, 0xFF), static_cast<std::uint8_t>(0xFF));
}

static void test_read_u8_checked_empty_vector() {
    std::vector<std::uint8_t> code;
    ASSERT_EQ(read_u8_checked(code, 0), static_cast<std::uint8_t>(0));
    ASSERT_EQ(read_u8_checked(code, 0, 0x42), static_cast<std::uint8_t>(0x42));
}

// ─── main ───

int main() {
    // read_u16_be
    RUN(test_read_u16_be_zero);
    RUN(test_read_u16_be_one);
    RUN(test_read_u16_be_high_byte_only);
    RUN(test_read_u16_be_max);
    RUN(test_read_u16_be_known_value);

    // read_u32_be
    RUN(test_read_u32_be_zero);
    RUN(test_read_u32_be_one);
    RUN(test_read_u32_be_max);
    RUN(test_read_u32_be_known_value);
    RUN(test_read_u32_be_high_byte_only);

    // write_u16_be
    RUN(test_write_u16_be_zero);
    RUN(test_write_u16_be_max);
    RUN(test_write_u16_be_known_value);
    RUN(test_write_u16_be_appends);

    // Round-trip
    RUN(test_u16_round_trip);

    // read_u8_checked
    RUN(test_read_u8_checked_in_bounds);
    RUN(test_read_u8_checked_out_of_bounds_default);
    RUN(test_read_u8_checked_out_of_bounds_custom_default);
    RUN(test_read_u8_checked_empty_vector);

    return SUMMARY();
}
