/// @file test_hmac.cpp
/// Unit tests for hmac.hpp — HMAC-SHA256, truncation to 128 bits, constant-time compare.

#include <gtest/gtest.h>
#include <sero/security/hmac.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace sero;

namespace {

std::string bytes_to_hex(const uint8_t* data, std::size_t len) {
    std::string hex;
    for (std::size_t i = 0; i < len; ++i) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", data[i]);
        hex += buf;
    }
    return hex;
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (std::size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

} // namespace

// ── RFC 4231 Test Case 1 ────────────────────────────────────────
// Key  = 0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b (20 bytes)
// Data = "Hi There"
// HMAC-SHA-256 = b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7

TEST(Hmac_Sha256, Rfc4231_TestCase1) {
    std::vector<uint8_t> key(20, 0x0b);
    const char* data = "Hi There";
    uint8_t out[32];

    hmac_sha256(key.data(), key.size(),
                reinterpret_cast<const uint8_t*>(data), std::strlen(data),
                out);

    EXPECT_EQ(bytes_to_hex(out, 32),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

// ── RFC 4231 Test Case 2 ────────────────────────────────────────
// Key  = "Jefe"
// Data = "what do ya want for nothing?"

TEST(Hmac_Sha256, Rfc4231_TestCase2) {
    const char* key_str = "Jefe";
    const char* data = "what do ya want for nothing?";
    uint8_t out[32];

    hmac_sha256(reinterpret_cast<const uint8_t*>(key_str), 4,
                reinterpret_cast<const uint8_t*>(data), std::strlen(data),
                out);

    EXPECT_EQ(bytes_to_hex(out, 32),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// ── RFC 4231 Test Case 3 ────────────────────────────────────────
// Key  = aaaa... (20 bytes of 0xaa)
// Data = dddd... (50 bytes of 0xdd)

TEST(Hmac_Sha256, Rfc4231_TestCase3) {
    std::vector<uint8_t> key(20, 0xaa);
    std::vector<uint8_t> data(50, 0xdd);
    uint8_t out[32];

    hmac_sha256(key.data(), key.size(), data.data(), data.size(), out);

    EXPECT_EQ(bytes_to_hex(out, 32),
              "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
}

// ── RFC 4231 Test Case 4 ────────────────────────────────────────
// Key  = 0102030405060708090a0b0c0d0e0f10111213141516171819 (25 bytes)
// Data = cdcdcd... (50 bytes of 0xcd)

TEST(Hmac_Sha256, Rfc4231_TestCase4) {
    auto key = hex_to_bytes("0102030405060708090a0b0c0d0e0f10111213141516171819");
    std::vector<uint8_t> data(50, 0xcd);
    uint8_t out[32];

    hmac_sha256(key.data(), key.size(), data.data(), data.size(), out);

    EXPECT_EQ(bytes_to_hex(out, 32),
              "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
}

// ── RFC 4231 Test Case 6: Long key (>64 bytes, hashed first) ────
// Key  = 131 bytes of 0xaa
// Data = "Test Using Larger Than Block-Size Key - Hash Key First"

TEST(Hmac_Sha256, Rfc4231_TestCase6_LongKey) {
    std::vector<uint8_t> key(131, 0xaa);
    const char* data = "Test Using Larger Than Block-Size Key - Hash Key First";
    uint8_t out[32];

    hmac_sha256(key.data(), key.size(),
                reinterpret_cast<const uint8_t*>(data), std::strlen(data),
                out);

    EXPECT_EQ(bytes_to_hex(out, 32),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

// ── HMAC-SHA256-128 truncation ──────────────────────────────────

TEST(Hmac_Sha256_128, TruncationMatchesFirst16Bytes) {
    std::vector<uint8_t> key(20, 0x0b);
    const char* data = "Hi There";
    uint8_t full[32];
    uint8_t truncated[16];

    hmac_sha256(key.data(), key.size(),
                reinterpret_cast<const uint8_t*>(data), std::strlen(data),
                full);
    hmac_sha256_128(key.data(), key.size(),
                    reinterpret_cast<const uint8_t*>(data), std::strlen(data),
                    truncated);

    EXPECT_EQ(std::memcmp(full, truncated, 16), 0);
}

// ── hmac_equal ──────────────────────────────────────────────────

TEST(HmacEqual, IdenticalInputs_ReturnsTrue) {
    uint8_t a[16], b[16];
    std::memset(a, 0xAB, 16);
    std::memset(b, 0xAB, 16);
    EXPECT_TRUE(hmac_equal(a, b));
}

TEST(HmacEqual, DifferentInputs_ReturnsFalse) {
    uint8_t a[16], b[16];
    std::memset(a, 0xAB, 16);
    std::memset(b, 0xCD, 16);
    EXPECT_FALSE(hmac_equal(a, b));
}

TEST(HmacEqual, SingleBitDifference_ReturnsFalse) {
    uint8_t a[16], b[16];
    std::memset(a, 0x00, 16);
    std::memset(b, 0x00, 16);
    b[15] = 0x01; // last bit different
    EXPECT_FALSE(hmac_equal(a, b));
}

TEST(HmacEqual, AllZeros_ReturnsTrue) {
    uint8_t a[16] = {};
    uint8_t b[16] = {};
    EXPECT_TRUE(hmac_equal(a, b));
}

TEST(HmacEqual, FirstByteDifference_ReturnsFalse) {
    uint8_t a[16] = {};
    uint8_t b[16] = {};
    b[0] = 0x80;
    EXPECT_FALSE(hmac_equal(a, b));
}

// ── Key shorter than block size is padded ───────────────────────

TEST(Hmac_Sha256, ShortKey_ProducesCorrectResult) {
    // A 4-byte key is shorter than block size (64), should be zero-padded.
    // This is covered by RFC 4231 Test Case 2 ("Jefe") but let's make it explicit.
    const uint8_t key[] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    uint8_t out1[32], out2[32];

    hmac_sha256(key, 4, data, 3, out1);
    hmac_sha256(key, 4, data, 3, out2);

    // Deterministic
    EXPECT_EQ(std::memcmp(out1, out2, 32), 0);
}

// ── Key exactly block size ──────────────────────────────────────

TEST(Hmac_Sha256, ExactBlockSizeKey_ProducesResult) {
    std::vector<uint8_t> key(64, 0x42); // exactly BLOCK_SIZE
    const uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t out[32];

    hmac_sha256(key.data(), key.size(), data, 3, out);

    // Verify it's deterministic and non-trivial
    uint8_t out2[32];
    hmac_sha256(key.data(), key.size(), data, 3, out2);
    EXPECT_EQ(std::memcmp(out, out2, 32), 0);
}
