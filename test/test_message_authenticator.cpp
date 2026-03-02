/// @file test_message_authenticator.cpp
/// Unit tests for message_authenticator.hpp — key management, compute, verify.

#include <gtest/gtest.h>
#include <sero/security/message_authenticator.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::make_addr;

using Auth = MessageAuthenticator<SmallConfig>;
using Addr = Address<SmallConfig>;

namespace {

// Helper to make a 32-byte key filled with a single value
std::array<uint8_t, SmallConfig::HmacKeySize> make_key(uint8_t fill) {
    std::array<uint8_t, SmallConfig::HmacKeySize> k;
    k.fill(fill);
    return k;
}

} // namespace

// ── set_key ─────────────────────────────────────────────────────

TEST(MessageAuthenticator_SetKey, Success) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    EXPECT_TRUE(auth.set_key(peer, key.data()));
    EXPECT_TRUE(auth.has_key(peer));
}

TEST(MessageAuthenticator_SetKey, UpdateExisting) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key1 = make_key(0xAA);
    auto key2 = make_key(0xBB);

    EXPECT_TRUE(auth.set_key(peer, key1.data()));
    EXPECT_TRUE(auth.set_key(peer, key2.data())); // update

    // Verify the new key is used (compute with updated key should differ)
    const uint8_t header[20] = {0x01};
    const uint8_t payload[] = {0x42};
    uint8_t hmac1[16], hmac2[16];

    // We can't directly check with key1 anymore, but we can verify it works
    EXPECT_TRUE(auth.compute(header, payload, 1, peer, hmac1));
    // Compute again — should be deterministic
    EXPECT_TRUE(auth.compute(header, payload, 1, peer, hmac2));
    EXPECT_EQ(std::memcmp(hmac1, hmac2, 16), 0);
}

TEST(MessageAuthenticator_SetKey, TableFull_ReturnsFalse) {
    Auth auth;
    // Fill MaxTrackedPeers(4) keys
    for (uint8_t i = 0; i < SmallConfig::MaxTrackedPeers; ++i) {
        Addr peer = make_addr(i);
        auto key = make_key(i);
        EXPECT_TRUE(auth.set_key(peer, key.data()));
    }
    // One more → false
    Addr overflow = make_addr(99);
    auto key = make_key(0xFF);
    EXPECT_FALSE(auth.set_key(overflow, key.data()));
}

// ── has_key ─────────────────────────────────────────────────────

TEST(MessageAuthenticator_HasKey, KnownPeer_ReturnsTrue) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    auth.set_key(peer, key.data());
    EXPECT_TRUE(auth.has_key(peer));
}

TEST(MessageAuthenticator_HasKey, UnknownPeer_ReturnsFalse) {
    Auth auth;
    Addr peer = make_addr(99);
    EXPECT_FALSE(auth.has_key(peer));
}

// ── compute ─────────────────────────────────────────────────────

TEST(MessageAuthenticator_Compute, UnknownPeer_ReturnsFalse) {
    Auth auth;
    Addr peer = make_addr(1);
    const uint8_t header[20] = {};
    const uint8_t payload[] = {0x01};
    uint8_t hmac[16];
    EXPECT_FALSE(auth.compute(header, payload, 1, peer, hmac));
}

TEST(MessageAuthenticator_Compute, KnownPeer_ProducesHmac) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    auth.set_key(peer, key.data());

    const uint8_t header[20] = {0x01, 0x00, 0x00, 0x01};
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t hmac[16] = {};

    EXPECT_TRUE(auth.compute(header, payload, 4, peer, hmac));

    // HMAC should be non-zero
    bool all_zero = true;
    for (int i = 0; i < 16; ++i) {
        if (hmac[i] != 0) { all_zero = false; break; }
    }
    EXPECT_FALSE(all_zero);
}

// ── verify ──────────────────────────────────────────────────────

TEST(MessageAuthenticator_Verify, CorrectHmac_ReturnsTrue) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    auth.set_key(peer, key.data());

    const uint8_t header[20] = {0x01, 0x00, 0x00};
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t hmac[16];

    auth.compute(header, payload, 3, peer, hmac);
    EXPECT_TRUE(auth.verify(header, payload, 3, peer, hmac));
}

TEST(MessageAuthenticator_Verify, TamperedHmac_ReturnsFalse) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    auth.set_key(peer, key.data());

    const uint8_t header[20] = {0x01, 0x00};
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t hmac[16];

    auth.compute(header, payload, 3, peer, hmac);
    hmac[0] ^= 0x01; // tamper
    EXPECT_FALSE(auth.verify(header, payload, 3, peer, hmac));
}

TEST(MessageAuthenticator_Verify, UnknownPeer_ReturnsFalse) {
    Auth auth;
    Addr peer = make_addr(1);
    const uint8_t header[20] = {};
    const uint8_t payload[] = {0x01};
    const uint8_t hmac[16] = {};
    EXPECT_FALSE(auth.verify(header, payload, 1, peer, hmac));
}

TEST(MessageAuthenticator_Verify, TamperedPayload_ReturnsFalse) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0xAB);
    auth.set_key(peer, key.data());

    const uint8_t header[20] = {0x01};
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t hmac[16];

    auth.compute(header, payload, 3, peer, hmac);

    // Tamper with payload
    payload[1] = 0xFF;
    EXPECT_FALSE(auth.verify(header, payload, 3, peer, hmac));
}

// ── Compute/Verify round-trip ───────────────────────────────────

TEST(MessageAuthenticator_ComputeVerify, RoundTrip) {
    Auth auth;
    Addr peer = make_addr(1);
    auto key = make_key(0x42);
    auth.set_key(peer, key.data());

    // Build a realistic header
    MessageHeader hdr;
    hdr.version = PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.flags = FLAG_AUTH;
    hdr.service_id = 0x1234;
    hdr.method_event_id = 0x0001;
    hdr.client_id = 0x0010;
    hdr.payload_length = 4;

    uint8_t header_bytes[20];
    hdr.serialize(header_bytes);

    const uint8_t payload[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t hmac[16];

    EXPECT_TRUE(auth.compute(header_bytes, payload, 4, peer, hmac));
    EXPECT_TRUE(auth.verify(header_bytes, payload, 4, peer, hmac));
}
