/// @file test_sd_payloads.cpp
/// Unit tests for service_discovery.hpp SD payload serialization/deserialization.

#include <gtest/gtest.h>
#include <sero/service/service_discovery.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::make_addr;

using Addr = Address<SmallConfig>;

// ── SD Offer serialize/deserialize round-trip ───────────────────

TEST(SdPayload_Offer, SerializeDeserialize_RoundTrip) {
    Addr provider = make_addr(0x42);
    uint8_t buf[SD_OFFER_PAYLOAD_SIZE<SmallConfig>];

    sd_payload::serialize_offer<SmallConfig>(
        buf, 0x1234, 2, 5, 300, 0xABCD, provider);

    uint16_t sid; uint8_t major, minor; uint16_t ttl, session_id;
    Addr out_addr{};
    ASSERT_TRUE(sd_payload::deserialize_offer<SmallConfig>(
        buf, sizeof(buf), sid, major, minor, ttl, session_id, out_addr));

    EXPECT_EQ(sid, 0x1234);
    EXPECT_EQ(major, 2);
    EXPECT_EQ(minor, 5);
    EXPECT_EQ(ttl, 300);
    EXPECT_EQ(session_id, 0xABCD);
    EXPECT_EQ(out_addr, provider);
}

TEST(SdPayload_Offer, Deserialize_TooShort_ReturnsFalse) {
    const uint8_t buf[4] = {}; // too short
    uint16_t sid; uint8_t major, minor; uint16_t ttl, session_id;
    Addr addr{};
    EXPECT_FALSE(sd_payload::deserialize_offer<SmallConfig>(
        buf, 4, sid, major, minor, ttl, session_id, addr));
}

TEST(SdPayload_Offer, Deserialize_ExactSize_ReturnsTrue) {
    uint8_t buf[SD_OFFER_PAYLOAD_SIZE<SmallConfig>] = {};
    sd_payload::serialize_offer<SmallConfig>(buf, 1, 1, 0, 5, 1, make_addr(1));

    uint16_t sid; uint8_t major, minor; uint16_t ttl, session_id;
    Addr addr{};
    EXPECT_TRUE(sd_payload::deserialize_offer<SmallConfig>(
        buf, SD_OFFER_PAYLOAD_SIZE<SmallConfig>,
        sid, major, minor, ttl, session_id, addr));
}

// ── SD Find serialize/deserialize round-trip ────────────────────

TEST(SdPayload_Find, SerializeDeserialize_RoundTrip) {
    uint8_t buf[SD_FIND_PAYLOAD_SIZE];
    sd_payload::serialize_find(buf, 0x5678);

    uint16_t requested_sid;
    ASSERT_TRUE(sd_payload::deserialize_find(buf, sizeof(buf), requested_sid));
    EXPECT_EQ(requested_sid, 0x5678);
}

TEST(SdPayload_Find, Deserialize_TooShort_ReturnsFalse) {
    const uint8_t buf[2] = {};
    uint16_t sid;
    EXPECT_FALSE(sd_payload::deserialize_find(buf, 2, sid));
}

TEST(SdPayload_Find, WildcardServiceId) {
    uint8_t buf[SD_FIND_PAYLOAD_SIZE];
    sd_payload::serialize_find(buf, 0xFFFF);

    uint16_t sid;
    ASSERT_TRUE(sd_payload::deserialize_find(buf, sizeof(buf), sid));
    EXPECT_EQ(sid, 0xFFFF);
}

// ── SD Subscribe serialize/deserialize round-trip ───────────────

TEST(SdPayload_Subscribe, SerializeDeserialize_RoundTrip) {
    uint8_t buf[SD_SUBSCRIBE_PAYLOAD_SIZE];
    sd_payload::serialize_subscribe(buf, 0x1000, 0x8001, 60);

    uint16_t sid, eid, ttl;
    ASSERT_TRUE(sd_payload::deserialize_subscribe(buf, sizeof(buf), sid, eid, ttl));
    EXPECT_EQ(sid, 0x1000);
    EXPECT_EQ(eid, 0x8001);
    EXPECT_EQ(ttl, 60);
}

TEST(SdPayload_Subscribe, Deserialize_TooShort_ReturnsFalse) {
    const uint8_t buf[4] = {};
    uint16_t sid, eid, ttl;
    EXPECT_FALSE(sd_payload::deserialize_subscribe(buf, 4, sid, eid, ttl));
}

TEST(SdPayload_Subscribe, TtlZero) {
    uint8_t buf[SD_SUBSCRIBE_PAYLOAD_SIZE];
    sd_payload::serialize_subscribe(buf, 0x1000, 0x8001, 0);

    uint16_t sid, eid, ttl;
    ASSERT_TRUE(sd_payload::deserialize_subscribe(buf, sizeof(buf), sid, eid, ttl));
    EXPECT_EQ(ttl, 0);
}

// ── Payload size constants ──────────────────────────────────────

TEST(SdPayload_Sizes, OfferSize_Correct) {
    // 8 + TransportAddressSize(8) = 16
    EXPECT_EQ(SD_OFFER_PAYLOAD_SIZE<SmallConfig>, 16u);
}

TEST(SdPayload_Sizes, FindSize_Correct) {
    EXPECT_EQ(SD_FIND_PAYLOAD_SIZE, 4u);
}

TEST(SdPayload_Sizes, SubscribeSize_Correct) {
    EXPECT_EQ(SD_SUBSCRIBE_PAYLOAD_SIZE, 8u);
}
