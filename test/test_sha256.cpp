/// @file test_sha256.cpp
/// Unit tests for sha256.hpp — SHA-256 against NIST test vectors.

#include <gtest/gtest.h>
#include <sero/security/sha256.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace sero;

namespace {

// Helper to convert hex string to bytes
std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (std::size_t i = 0; i < hex.length(); i += 2) {
        uint8_t b = static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16));
        bytes.push_back(b);
    }
    return bytes;
}

// Helper to convert digest to hex string
std::string bytes_to_hex(const uint8_t* data, std::size_t len) {
    std::string hex;
    for (std::size_t i = 0; i < len; ++i) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", data[i]);
        hex += buf;
    }
    return hex;
}

} // namespace

// ── NIST Test Vectors ───────────────────────────────────────────

TEST(Sha256_Hash, EmptyInput) {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    uint8_t digest[Sha256::DIGEST_SIZE];
    Sha256::hash(nullptr, 0, digest);

    EXPECT_EQ(bytes_to_hex(digest, 32),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256_Hash, Abc) {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const uint8_t msg[] = "abc";
    uint8_t digest[Sha256::DIGEST_SIZE];
    Sha256::hash(msg, 3, digest);

    EXPECT_EQ(bytes_to_hex(digest, 32),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256_Hash, TwoBlockMessage) {
    // SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    // = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t digest[Sha256::DIGEST_SIZE];
    Sha256::hash(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), digest);

    EXPECT_EQ(bytes_to_hex(digest, 32),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// ── Incremental update matches single-shot ──────────────────────

TEST(Sha256_IncrementalUpdate, MatchesSingleShot) {
    const char* full_msg = "The quick brown fox jumps over the lazy dog";
    std::size_t len = std::strlen(full_msg);

    // Single shot
    uint8_t digest_single[Sha256::DIGEST_SIZE];
    Sha256::hash(reinterpret_cast<const uint8_t*>(full_msg), len, digest_single);

    // Incremental: split at various points
    Sha256 ctx;
    ctx.update(reinterpret_cast<const uint8_t*>(full_msg), 10);
    ctx.update(reinterpret_cast<const uint8_t*>(full_msg + 10), 20);
    ctx.update(reinterpret_cast<const uint8_t*>(full_msg + 30), len - 30);
    uint8_t digest_inc[Sha256::DIGEST_SIZE];
    ctx.finalize(digest_inc);

    EXPECT_EQ(std::memcmp(digest_single, digest_inc, Sha256::DIGEST_SIZE), 0);
}

TEST(Sha256_IncrementalUpdate, ByteByByte_MatchesSingleShot) {
    const char* msg = "abcdef";
    std::size_t len = 6;

    uint8_t digest_single[Sha256::DIGEST_SIZE];
    Sha256::hash(reinterpret_cast<const uint8_t*>(msg), len, digest_single);

    Sha256 ctx;
    for (std::size_t i = 0; i < len; ++i) {
        ctx.update(reinterpret_cast<const uint8_t*>(msg + i), 1);
    }
    uint8_t digest_inc[Sha256::DIGEST_SIZE];
    ctx.finalize(digest_inc);

    EXPECT_EQ(std::memcmp(digest_single, digest_inc, Sha256::DIGEST_SIZE), 0);
}

// ── Multi-block input (>64 bytes) ───────────────────────────────

TEST(Sha256_Hash, LargeInput_128Bytes) {
    std::vector<uint8_t> data(128, 0x41); // 128 'A's
    uint8_t digest[Sha256::DIGEST_SIZE];
    Sha256::hash(data.data(), data.size(), digest);

    // Just verify it produces some non-trivial output and is deterministic
    uint8_t digest2[Sha256::DIGEST_SIZE];
    Sha256::hash(data.data(), data.size(), digest2);
    EXPECT_EQ(std::memcmp(digest, digest2, Sha256::DIGEST_SIZE), 0);
}

// ── Reset and reuse ─────────────────────────────────────────────

TEST(Sha256_Reset, ResetAndRehash_ProducesCorrectResult) {
    Sha256 ctx;

    // First hash
    const uint8_t msg1[] = "abc";
    ctx.update(msg1, 3);
    uint8_t digest1[Sha256::DIGEST_SIZE];
    ctx.finalize(digest1);

    // Reset and hash different data
    ctx.reset();
    const uint8_t msg2[] = "def";
    ctx.update(msg2, 3);
    uint8_t digest2[Sha256::DIGEST_SIZE];
    ctx.finalize(digest2);

    // Verify second hash matches single-shot hash of "def"
    uint8_t expected[Sha256::DIGEST_SIZE];
    Sha256::hash(msg2, 3, expected);
    EXPECT_EQ(std::memcmp(digest2, expected, Sha256::DIGEST_SIZE), 0);

    // And that digest1 != digest2
    EXPECT_NE(std::memcmp(digest1, digest2, Sha256::DIGEST_SIZE), 0);
}

// ── Known vector: "The quick brown fox..." ──────────────────────

TEST(Sha256_Hash, QuickBrownFox) {
    // SHA-256("The quick brown fox jumps over the lazy dog")
    // = d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592
    const char* msg = "The quick brown fox jumps over the lazy dog";
    uint8_t digest[Sha256::DIGEST_SIZE];
    Sha256::hash(reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), digest);

    EXPECT_EQ(bytes_to_hex(digest, 32),
              "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}
