/// @file test_crc16.cpp
/// Unit tests for crc16.hpp — CRC-16/CCITT-FALSE compute, append, verify.

#include <gtest/gtest.h>
#include <sero/security/crc16.hpp>

#include <cstring>
#include <vector>

using namespace sero;

// ── crc16_compute ───────────────────────────────────────────────

TEST(Crc16_Compute, EmptyInput_Returns0xFFFF) {
    uint16_t crc = crc16_compute(nullptr, 0);
    EXPECT_EQ(crc, 0xFFFF);
}

TEST(Crc16_Compute, KnownVector_123456789_Returns0x29B1) {
    // Standard CRC-16/CCITT-FALSE test vector
    const uint8_t data[] = "123456789";
    uint16_t crc = crc16_compute(data, 9);
    EXPECT_EQ(crc, 0x29B1);
}

TEST(Crc16_Compute, SingleByte_0x00) {
    const uint8_t data[] = {0x00};
    uint16_t crc = crc16_compute(data, 1);
    // CRC should not be the init value
    EXPECT_NE(crc, 0xFFFF);
}

TEST(Crc16_Compute, DifferentData_DifferentCrc) {
    const uint8_t data1[] = {0x01, 0x02, 0x03};
    const uint8_t data2[] = {0x01, 0x02, 0x04};
    EXPECT_NE(crc16_compute(data1, 3), crc16_compute(data2, 3));
}

// ── crc16_append + crc16_verify round-trip ──────────────────────

TEST(Crc16_AppendAndVerify, RoundTrip_Succeeds) {
    uint8_t buffer[32] = {};
    const char* msg = "Hello, SOME/IP";
    std::size_t len = std::strlen(msg);
    std::memcpy(buffer, msg, len);

    crc16_append(buffer, len);
    EXPECT_TRUE(crc16_verify(buffer, len + 2));
}

TEST(Crc16_AppendAndVerify, AllZeros_RoundTrip) {
    uint8_t buffer[10] = {};
    crc16_append(buffer, 8);
    EXPECT_TRUE(crc16_verify(buffer, 10));
}

TEST(Crc16_AppendAndVerify, SingleByte_RoundTrip) {
    uint8_t buffer[3] = {0xAB};
    crc16_append(buffer, 1);
    EXPECT_TRUE(crc16_verify(buffer, 3));
}

// ── crc16_verify edge cases ─────────────────────────────────────

TEST(Crc16_Verify, TooShort_Length0_ReturnsFalse) {
    const uint8_t data[] = {0x01};
    EXPECT_FALSE(crc16_verify(data, 0));
}

TEST(Crc16_Verify, TooShort_Length1_ReturnsFalse) {
    const uint8_t data[] = {0x01};
    EXPECT_FALSE(crc16_verify(data, 1));
}

TEST(Crc16_Verify, Length2_EmptyPayload_ValidCrc) {
    // CRC of empty data = 0xFFFF → bytes {0xFF, 0xFF}
    const uint8_t data[] = {0xFF, 0xFF};
    EXPECT_TRUE(crc16_verify(data, 2));
}

TEST(Crc16_Verify, TamperedData_ReturnsFalse) {
    uint8_t buffer[16] = {};
    std::memcpy(buffer, "test", 4);
    crc16_append(buffer, 4);
    EXPECT_TRUE(crc16_verify(buffer, 6));

    // Tamper with one byte
    buffer[2] ^= 0x01;
    EXPECT_FALSE(crc16_verify(buffer, 6));
}

TEST(Crc16_Verify, TamperedCrc_ReturnsFalse) {
    uint8_t buffer[16] = {};
    std::memcpy(buffer, "test", 4);
    crc16_append(buffer, 4);

    // Tamper with CRC byte
    buffer[4] ^= 0x01;
    EXPECT_FALSE(crc16_verify(buffer, 6));
}

// ── crc16_append byte order ─────────────────────────────────────

TEST(Crc16_Append, BigEndianByteOrder) {
    uint8_t buffer[4] = {0xAB};
    crc16_append(buffer, 1);
    uint16_t crc = crc16_compute(buffer, 1);
    // CRC is appended big-endian
    EXPECT_EQ(buffer[1], static_cast<uint8_t>(crc >> 8));
    EXPECT_EQ(buffer[2], static_cast<uint8_t>(crc & 0xFF));
}
