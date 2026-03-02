/// @file test_service_discovery.cpp
/// Unit tests for service_discovery.hpp — SD state machines, TTL, reboot detection.

#include <gtest/gtest.h>
#include <sero/service/service_discovery.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::make_addr;

using SD = ServiceDiscovery<SmallConfig>;
using Addr = Address<SmallConfig>;

// ── Helper: capture callback invocations ────────────────────────

namespace {

struct SdCallbackState {
    int found_count = 0;
    int lost_count = 0;
    int sub_ack_count = 0;
    uint16_t last_found_sid = 0;
    Addr last_found_addr{};
    uint16_t last_lost_sid = 0;
    ReturnCode last_ack_result = ReturnCode::E_OK;
    uint16_t last_ack_ttl = 0;
};

void on_found(uint16_t sid, const Addr& addr, void* ctx) {
    auto* s = static_cast<SdCallbackState*>(ctx);
    ++s->found_count;
    s->last_found_sid = sid;
    s->last_found_addr = addr;
}

void on_lost(uint16_t sid, void* ctx) {
    auto* s = static_cast<SdCallbackState*>(ctx);
    ++s->lost_count;
    s->last_lost_sid = sid;
}

void on_sub_ack(uint16_t sid, uint16_t eid, ReturnCode result,
                uint16_t granted_ttl, void* ctx) {
    auto* s = static_cast<SdCallbackState*>(ctx);
    ++s->sub_ack_count;
    s->last_ack_result = result;
    s->last_ack_ttl = granted_ttl;
    (void)sid; (void)eid;
}

// Helper to set up callbacks
void setup_callbacks(SD& sd, SdCallbackState& state) {
    auto& cb = sd.callbacks();
    cb.on_service_found = on_found;
    cb.service_found_ctx = &state;
    cb.on_service_lost = on_lost;
    cb.service_lost_ctx = &state;
    cb.on_subscription_ack = on_sub_ack;
    cb.subscription_ack_ctx = &state;
}

struct BroadcastCapture {
    int count = 0;
    std::vector<std::vector<uint8_t>> messages;

    auto fn() {
        return [this](const uint8_t* data, std::size_t len) -> bool {
            ++count;
            messages.emplace_back(data, data + len);
            return true;
        };
    }
};

struct UnicastCapture {
    int count = 0;
    std::vector<std::pair<Addr, std::vector<uint8_t>>> messages;

    auto fn() {
        return [this](const Addr& dest, const uint8_t* data, std::size_t len) -> bool {
            ++count;
            messages.push_back({dest, std::vector<uint8_t>(data, data + len)});
            return true;
        };
    }
};

} // namespace

// ── Provider: offer/stop_offer ──────────────────────────────────

TEST(ServiceDiscovery_Offer, Success_SetsState) {
    SD sd;
    sd.set_local_address(make_addr(10));
    EXPECT_TRUE(sd.offer(0x1000, 1, 0, 5, 0));
}

TEST(ServiceDiscovery_Offer, TableFull_ReturnsFalse) {
    SD sd;
    for (std::size_t i = 0; i < SmallConfig::MaxServices; ++i) {
        EXPECT_TRUE(sd.offer(static_cast<uint16_t>(0x1000 + i), 1, 0, 5, 0));
    }
    EXPECT_FALSE(sd.offer(0x9999, 1, 0, 5, 0));
}

TEST(ServiceDiscovery_StopOffer, ClearsState) {
    SD sd;
    sd.offer(0x1000, 1, 0, 5, 0);
    sd.stop_offer(0x1000);

    BroadcastCapture bc;
    sd.process_offers(1000, bc.fn());
    EXPECT_EQ(bc.count, 0); // no longer offered
}

// ── Provider: process_offers broadcasts ─────────────────────────

TEST(ServiceDiscovery_ProcessOffers, BroadcastsInitialOffer) {
    SD sd;
    sd.set_local_address(make_addr(10));
    sd.set_client_id(0x0001);
    sd.offer(0x1000, 1, 0, 5, 0);

    BroadcastCapture bc;
    sd.process_offers(0, bc.fn());
    EXPECT_EQ(bc.count, 1);
}

TEST(ServiceDiscovery_ProcessOffers, ReBroadcastsAtHalfTtl) {
    SD sd;
    sd.set_local_address(make_addr(10));
    sd.set_client_id(0x0001);
    sd.offer(0x1000, 1, 0, 4, 0); // TTL=4s → re-broadcast at 2s

    BroadcastCapture bc1;
    sd.process_offers(0, bc1.fn()); // initial
    EXPECT_EQ(bc1.count, 1);

    BroadcastCapture bc2;
    sd.process_offers(1000, bc2.fn()); // too early (1s < 2s)
    EXPECT_EQ(bc2.count, 0);

    BroadcastCapture bc3;
    sd.process_offers(2500, bc3.fn()); // after TTL/2 (2.5s >= 2s)
    EXPECT_EQ(bc3.count, 1);
}

// ── Consumer: find ──────────────────────────────────────────────

TEST(ServiceDiscovery_Find, InitiatesSearch) {
    SD sd;
    sd.set_client_id(0x0001);
    EXPECT_TRUE(sd.find(0x1000, 1, 0));
    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::SEARCHING);
}

TEST(ServiceDiscovery_Find, TableFull_ReturnsFalse) {
    SD sd;
    for (std::size_t i = 0; i < SmallConfig::MaxKnownServices; ++i) {
        EXPECT_TRUE(sd.find(static_cast<uint16_t>(0x1000 + i), 1, 0));
    }
    EXPECT_FALSE(sd.find(0x9999, 1, 0));
}

// ── Consumer: process_finds sends find messages with retries ────

TEST(ServiceDiscovery_ProcessFinds, SendsFindMessages) {
    SD sd;
    sd.set_client_id(0x0001);
    sd.find(0x1000, 1, 0);

    BroadcastCapture bc;
    sd.process_finds(0, bc.fn());
    EXPECT_GE(bc.count, 1);
}

TEST(ServiceDiscovery_ProcessFinds, RetriesExhausted_TransitionsToNotFound) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);

    // Exhaust retries (SdFindRetryCount=2, so 3 sends total: initial + 2 retries)
    BroadcastCapture bc;
    uint32_t time = 0;
    for (int i = 0; i < 20; ++i) { // enough iterations to exhaust retries
        sd.process_finds(time, bc.fn());
        time += 500; // advance time generously to trigger all retries
    }

    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::NOT_FOUND);
    EXPECT_GE(state.lost_count, 1);
}

// ── Consumer: handle_offer ──────────────────────────────────────

TEST(ServiceDiscovery_HandleOffer, ConsumerTransitionsToFound) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);
    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::SEARCHING);

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider, 100);

    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::FOUND);
    EXPECT_EQ(state.found_count, 1);
    EXPECT_EQ(state.last_found_sid, 0x1000);
    EXPECT_EQ(state.last_found_addr, provider);
}

TEST(ServiceDiscovery_HandleOffer, VersionMismatch_Ignored) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 2, 0); // expect major=2

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider, 100); // major=1 → mismatch

    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::SEARCHING);
    EXPECT_EQ(state.found_count, 0);
}

TEST(ServiceDiscovery_HandleOffer, TtlZero_Ignored) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 0, 0x0001, provider, 100); // TTL=0

    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::SEARCHING);
    EXPECT_EQ(state.found_count, 0);
}

TEST(ServiceDiscovery_HandleOffer, NotInterested_Ignored) {
    SD sd;
    sd.set_client_id(0x0001);
    // Don't call find() for 0x2000
    Addr provider = make_addr(42);
    sd.handle_offer(0x2000, 1, 5, 0x0001, provider, 100);
    // Should not crash, state is NOT_FOUND (default)
    EXPECT_EQ(sd.get_consumer_state(0x2000), SD::ConsumerState::NOT_FOUND);
}

// ── Reboot detection ────────────────────────────────────────────

TEST(ServiceDiscovery_HandleOffer, RebootDetection_LostThenFound) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);

    Addr provider = make_addr(42);
    // First offer: session_id = 1
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider, 100);
    EXPECT_EQ(state.found_count, 1);
    EXPECT_EQ(state.lost_count, 0);

    // Second offer from same provider with different session_id → reboot
    sd.handle_offer(0x1000, 1, 5, 0x0002, provider, 200);
    EXPECT_EQ(state.lost_count, 1);  // on_service_lost called
    EXPECT_EQ(state.found_count, 2); // then on_service_found called
}

TEST(ServiceDiscovery_DetectReboot, DetectsSessionIdChange) {
    SD sd;
    sd.set_client_id(0x0001);
    sd.find(0x1000, 1, 0);

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider, 100);

    // detect_reboot checks if a new session_id differs
    EXPECT_TRUE(sd.detect_reboot(0x1000, 0x0002));
    EXPECT_FALSE(sd.detect_reboot(0x1000, 0x0001)); // same session
}

// ── Consumer TTL expiry ─────────────────────────────────────────

TEST(ServiceDiscovery_ProcessConsumerTtls, Expiry_TransitionsToNotFound) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 2, 0x0001, provider, 1000); // TTL=2s → expires at 3000

    std::vector<Addr> expired_peers;
    auto on_expired = [&](const Addr& addr) { expired_peers.push_back(addr); };

    sd.process_consumer_ttls(2000, on_expired); // not yet
    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::FOUND);

    sd.process_consumer_ttls(3500, on_expired); // expired
    EXPECT_EQ(sd.get_consumer_state(0x1000), SD::ConsumerState::NOT_FOUND);
    EXPECT_EQ(state.lost_count, 1);
    EXPECT_EQ(expired_peers.size(), 1u);
    EXPECT_EQ(expired_peers[0], provider);
}

// ── Provider: handle_find ───────────────────────────────────────

TEST(ServiceDiscovery_HandleFind, ProviderRespondsWithOffer) {
    SD sd;
    sd.set_local_address(make_addr(10));
    sd.set_client_id(0x0001);
    sd.offer(0x1000, 1, 0, 5, 0);

    Addr requester = make_addr(50);
    UnicastCapture uc;
    sd.handle_find(0x1000, requester, uc.fn());

    EXPECT_EQ(uc.count, 1);
    EXPECT_EQ(uc.messages[0].first, requester);
}

TEST(ServiceDiscovery_HandleFind, WildcardRespondsWithAllServices) {
    SD sd;
    sd.set_local_address(make_addr(10));
    sd.set_client_id(0x0001);
    sd.offer(0x1000, 1, 0, 5, 0);
    sd.offer(0x2000, 2, 1, 5, 0);

    Addr requester = make_addr(50);
    UnicastCapture uc;
    sd.handle_find(0xFFFF, requester, uc.fn()); // wildcard

    EXPECT_EQ(uc.count, 2);
}

TEST(ServiceDiscovery_HandleFind, NoMatchingService_NoResponse) {
    SD sd;
    sd.set_local_address(make_addr(10));
    sd.set_client_id(0x0001);
    sd.offer(0x1000, 1, 0, 5, 0);

    Addr requester = make_addr(50);
    UnicastCapture uc;
    sd.handle_find(0x9999, requester, uc.fn()); // doesn't match

    EXPECT_EQ(uc.count, 0);
}

// ── get_provider_address ────────────────────────────────────────

TEST(ServiceDiscovery_GetProviderAddress, Found_ReturnsTrue) {
    SD sd;
    sd.set_client_id(0x0001);
    sd.find(0x1000, 1, 0);

    Addr provider = make_addr(42);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider, 100);

    Addr out{};
    EXPECT_TRUE(sd.get_provider_address(0x1000, out));
    EXPECT_EQ(out, provider);
}

TEST(ServiceDiscovery_GetProviderAddress, NotFound_ReturnsFalse) {
    SD sd;
    Addr out{};
    EXPECT_FALSE(sd.get_provider_address(0x9999, out));
}

// ── Subscription ACK ────────────────────────────────────────────

TEST(ServiceDiscovery_HandleSubscribeAck, Ok_UpdatesTtl) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);
    sd.handle_offer(0x1000, 1, 5, 0x0001, make_addr(42), 100);
    sd.subscribe_event(0x1000, 0x8001, 30, 100);

    sd.handle_subscribe_ack(0x1000, 0x8001, ReturnCode::E_OK, 30);

    EXPECT_EQ(state.sub_ack_count, 1);
    EXPECT_EQ(state.last_ack_result, ReturnCode::E_OK);
    EXPECT_EQ(state.last_ack_ttl, 30);
}

TEST(ServiceDiscovery_HandleSubscribeAck, NotOk_DeactivatesSubscription) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);
    sd.handle_offer(0x1000, 1, 5, 0x0001, make_addr(42), 100);
    sd.subscribe_event(0x1000, 0x8001, 30, 100);

    sd.handle_subscribe_ack(0x1000, 0x8001, ReturnCode::E_NOT_OK, 0);

    EXPECT_EQ(state.sub_ack_count, 1);
    EXPECT_EQ(state.last_ack_result, ReturnCode::E_NOT_OK);
}

// ── subscribe_event / unsubscribe_event ─────────────────────────

TEST(ServiceDiscovery_SubscribeEvent, Success) {
    SD sd;
    sd.set_client_id(0x0001);
    sd.find(0x1000, 1, 0);
    sd.handle_offer(0x1000, 1, 5, 0x0001, make_addr(42), 100);

    EXPECT_TRUE(sd.subscribe_event(0x1000, 0x8001, 30, 100));
}

TEST(ServiceDiscovery_UnsubscribeEvent, Success) {
    SD sd;
    sd.set_client_id(0x0001);
    sd.find(0x1000, 1, 0);
    sd.handle_offer(0x1000, 1, 5, 0x0001, make_addr(42), 100);
    sd.subscribe_event(0x1000, 0x8001, 30, 100);

    EXPECT_TRUE(sd.unsubscribe_event(0x1000, 0x8001));
}

TEST(ServiceDiscovery_UnsubscribeEvent, NotSubscribed_ReturnsFalse) {
    SD sd;
    EXPECT_FALSE(sd.unsubscribe_event(0x1000, 0x8001));
}

// ── Provider address change ─────────────────────────────────────

TEST(ServiceDiscovery_HandleOffer, ProviderAddressChange_RenotifiesFound) {
    SD sd;
    sd.set_client_id(0x0001);
    SdCallbackState state;
    setup_callbacks(sd, state);

    sd.find(0x1000, 1, 0);

    // First provider
    Addr provider1 = make_addr(42);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider1, 100);
    EXPECT_EQ(state.found_count, 1);

    // Different provider address, same session_id
    Addr provider2 = make_addr(99);
    sd.handle_offer(0x1000, 1, 5, 0x0001, provider2, 200);
    EXPECT_EQ(state.found_count, 2); // re-notified

    Addr out{};
    sd.get_provider_address(0x1000, out);
    EXPECT_EQ(out, provider2);
}
