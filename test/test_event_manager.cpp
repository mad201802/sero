/// @file test_event_manager.cpp
/// Unit tests for event_manager.hpp — subscription, eviction, notification, TTL.

#include <algorithm>

#include <gtest/gtest.h>
#include <sero/service/event_manager.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::make_addr;

using EvtMgr = EventManager<SmallConfig>;
using Addr = Address<SmallConfig>;

// ── register_event ──────────────────────────────────────────────

TEST(EventManager_RegisterEvent, Success) {
    EvtMgr mgr;
    EXPECT_TRUE(mgr.register_event(0x1000, 0x8001));
}

TEST(EventManager_RegisterEvent, Duplicate_Idempotent) {
    EvtMgr mgr;
    EXPECT_TRUE(mgr.register_event(0x1000, 0x8001));
    EXPECT_TRUE(mgr.register_event(0x1000, 0x8001)); // same event, still true
}

TEST(EventManager_RegisterEvent, TableFull_ReturnsFalse) {
    EvtMgr mgr;
    // MAX_EVENTS = MaxServices(4) * MaxEvents(4) = 16
    for (uint16_t i = 0; i < SmallConfig::MaxServices * SmallConfig::MaxEvents; ++i) {
        EXPECT_TRUE(mgr.register_event(
            static_cast<uint16_t>(0x1000 + i),
            static_cast<uint16_t>(0x8000 + i)));
    }
    EXPECT_FALSE(mgr.register_event(0x9999, 0x8999));
}

// ── subscribe ───────────────────────────────────────────────────

TEST(EventManager_Subscribe, Success_ReturnsOk) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    ReturnCode rc = mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 10, 0);
    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_TRUE(mgr.has_subscribers(0x1000, 0x8001));
}

TEST(EventManager_Subscribe, UnregisteredEvent_ReturnsNotOk) {
    EvtMgr mgr;
    Addr addr = make_addr(1);
    ReturnCode rc = mgr.subscribe(0x9999, 0x8001, 0x0001, addr, 10, 0);
    EXPECT_EQ(rc, ReturnCode::E_NOT_OK);
}

TEST(EventManager_Subscribe, TableFull_ReturnsNotOk) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    // Fill subscriber table (MaxSubscribers=3)
    for (uint16_t i = 0; i < SmallConfig::MaxSubscribers; ++i) {
        Addr addr = make_addr(static_cast<uint8_t>(i + 1));
        EXPECT_EQ(mgr.subscribe(0x1000, 0x8001, static_cast<uint16_t>(i + 1),
                                addr, 10, 0),
                  ReturnCode::E_OK);
    }
    // One more → table full
    Addr overflow = make_addr(99);
    EXPECT_EQ(mgr.subscribe(0x1000, 0x8001, 0x00FF, overflow, 10, 0),
              ReturnCode::E_NOT_OK);
}

TEST(EventManager_Subscribe, Renewal_ResetsTtl) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 5, 1000); // expires at 6000

    // Re-subscribe with new TTL
    ReturnCode rc = mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 10, 3000); // now expires at 13000
    EXPECT_EQ(rc, ReturnCode::E_OK);

    // Verify the TTL was reset (not the old 6000)
    uint16_t ttl = mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 3000);
    EXPECT_EQ(ttl, 10); // 10 seconds remaining
}

TEST(EventManager_Subscribe, TtlZero_DefaultsToConfigValue) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 0, 1000);

    uint16_t ttl = mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 1000);
    EXPECT_EQ(ttl, SmallConfig::SubscriptionTtlSeconds);
}

// ── unsubscribe ─────────────────────────────────────────────────

TEST(EventManager_Unsubscribe, RemovesSubscriber) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 10, 0);
    EXPECT_TRUE(mgr.has_subscribers(0x1000, 0x8001));

    mgr.unsubscribe(0x1000, 0x8001, 0x0001);
    EXPECT_FALSE(mgr.has_subscribers(0x1000, 0x8001));
}

TEST(EventManager_Unsubscribe, NonExistentSubscriber_NoCrash) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);
    // Unsubscribe when no subscribers exist — should not crash
    mgr.unsubscribe(0x1000, 0x8001, 0x0001);
}

TEST(EventManager_Unsubscribe, NonExistentEvent_NoCrash) {
    EvtMgr mgr;
    mgr.unsubscribe(0x9999, 0x8001, 0x0001);
}

// ── evict_expired ───────────────────────────────────────────────

TEST(EventManager_EvictExpired, RemovesExpired_KeepsAlive) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr1 = make_addr(1);
    Addr addr2 = make_addr(2);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr1, 2, 1000);  // expires at 3000
    mgr.subscribe(0x1000, 0x8001, 0x0002, addr2, 10, 1000); // expires at 11000

    mgr.evict_expired(4000); // after 3000, before 11000

    // addr1 should be evicted, addr2 should remain
    EXPECT_TRUE(mgr.has_subscribers(0x1000, 0x8001));
    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 4000), 0); // evicted
    EXPECT_GT(mgr.get_granted_ttl(0x1000, 0x8001, 0x0002, 4000), 0); // alive
}

// ── notify ──────────────────────────────────────────────────────

TEST(EventManager_Notify, CallsFnForEachSubscriber) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr1 = make_addr(1);
    Addr addr2 = make_addr(2);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr1, 10, 0);
    mgr.subscribe(0x1000, 0x8001, 0x0002, addr2, 10, 0);

    int call_count = 0;
    std::vector<uint16_t> client_ids;
    mgr.notify(0x1000, 0x8001, [&](const Addr&, uint16_t cid) {
        ++call_count;
        client_ids.push_back(cid);
    });

    EXPECT_EQ(call_count, 2);
    EXPECT_TRUE(std::find(client_ids.begin(), client_ids.end(), uint16_t(0x0001)) != client_ids.end());
    EXPECT_TRUE(std::find(client_ids.begin(), client_ids.end(), uint16_t(0x0002)) != client_ids.end());
}

TEST(EventManager_Notify, NoSubscribers_DoesNotCallFn) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    int call_count = 0;
    mgr.notify(0x1000, 0x8001, [&](const Addr&, uint16_t) {
        ++call_count;
    });
    EXPECT_EQ(call_count, 0);
}

// ── has_subscribers ─────────────────────────────────────────────

TEST(EventManager_HasSubscribers, TrueWhenPresent) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);
    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 10, 0);
    EXPECT_TRUE(mgr.has_subscribers(0x1000, 0x8001));
}

TEST(EventManager_HasSubscribers, FalseWhenNone) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);
    EXPECT_FALSE(mgr.has_subscribers(0x1000, 0x8001));
}

TEST(EventManager_HasSubscribers, FalseForUnregisteredEvent) {
    EvtMgr mgr;
    EXPECT_FALSE(mgr.has_subscribers(0x9999, 0x8001));
}

// ── get_granted_ttl ─────────────────────────────────────────────

TEST(EventManager_GetGrantedTtl, CorrectRemaining) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 10, 1000); // expires at 11000

    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 1000), 10);
    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 6000), 5);
    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 10500), 0);
}

TEST(EventManager_GetGrantedTtl, ZeroAfterExpiry) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);

    Addr addr = make_addr(1);
    mgr.subscribe(0x1000, 0x8001, 0x0001, addr, 5, 1000); // expires at 6000

    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0001, 7000), 0);
}

TEST(EventManager_GetGrantedTtl, UnknownSubscriber_ReturnsZero) {
    EvtMgr mgr;
    mgr.register_event(0x1000, 0x8001);
    EXPECT_EQ(mgr.get_granted_ttl(0x1000, 0x8001, 0x0099, 0), 0);
}
