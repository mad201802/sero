/// @file test_request_tracker.cpp
/// Unit tests for request_tracker.hpp — allocate, complete, evict, callback.

#include <gtest/gtest.h>
#include <sero/service/request_tracker.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;

using Tracker = RequestTracker<SmallConfig>;

namespace {

struct CallbackRecord {
    int call_count = 0;
    ReturnCode last_rc = ReturnCode::E_OK;
    std::vector<uint8_t> last_payload;
};

void record_callback(ReturnCode rc, const uint8_t* payload,
                     std::size_t payload_length, void* user_ctx) {
    auto* rec = static_cast<CallbackRecord*>(user_ctx);
    ++rec->call_count;
    rec->last_rc = rc;
    if (payload && payload_length > 0) {
        rec->last_payload.assign(payload, payload + payload_length);
    } else {
        rec->last_payload.clear();
    }
}

} // namespace

// ── allocate ────────────────────────────────────────────────────

TEST(RequestTracker_Allocate, ReturnsIncrementingIds) {
    Tracker tracker;
    CallbackRecord rec;

    auto id1 = tracker.allocate(0x1000, 0x0001, 500, 0, record_callback, &rec);
    auto id2 = tracker.allocate(0x1000, 0x0002, 500, 0, record_callback, &rec);
    auto id3 = tracker.allocate(0x1000, 0x0003, 500, 0, record_callback, &rec);

    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    ASSERT_TRUE(id3.has_value());
    EXPECT_EQ(*id1, 1u);
    EXPECT_EQ(*id2, 2u);
    EXPECT_EQ(*id3, 3u);
}

TEST(RequestTracker_Allocate, TableFull_ReturnsNullopt) {
    Tracker tracker;
    CallbackRecord rec;

    // Fill all MaxPendingRequests(4) slots
    for (std::size_t i = 0; i < SmallConfig::MaxPendingRequests; ++i) {
        EXPECT_TRUE(tracker.allocate(0x1000, 0x0001, 500, 0,
                                     record_callback, &rec).has_value());
    }
    // One more → nullopt
    EXPECT_FALSE(tracker.allocate(0x1000, 0x0001, 500, 0,
                                  record_callback, &rec).has_value());
}

// ── complete ────────────────────────────────────────────────────

TEST(RequestTracker_Complete, ValidId_InvokesCallback) {
    Tracker tracker;
    CallbackRecord rec;

    auto id = tracker.allocate(0x1000, 0x0001, 500, 0, record_callback, &rec);
    ASSERT_TRUE(id.has_value());

    const uint8_t payload[] = {0xDE, 0xAD};
    bool found = tracker.complete(*id, ReturnCode::E_OK, payload, 2);

    EXPECT_TRUE(found);
    EXPECT_EQ(rec.call_count, 1);
    EXPECT_EQ(rec.last_rc, ReturnCode::E_OK);
    EXPECT_EQ(rec.last_payload.size(), 2u);
    EXPECT_EQ(rec.last_payload[0], 0xDE);
}

TEST(RequestTracker_Complete, UnknownId_ReturnsFalse) {
    Tracker tracker;
    EXPECT_FALSE(tracker.complete(999, ReturnCode::E_OK, nullptr, 0));
}

TEST(RequestTracker_Complete, FreesSlot) {
    Tracker tracker;
    CallbackRecord rec;

    // Fill table
    std::vector<uint32_t> ids;
    for (std::size_t i = 0; i < SmallConfig::MaxPendingRequests; ++i) {
        auto id = tracker.allocate(0x1000, 0x0001, 500, 0, record_callback, &rec);
        ASSERT_TRUE(id.has_value());
        ids.push_back(*id);
    }
    EXPECT_FALSE(tracker.allocate(0x1000, 0x0001, 500, 0,
                                  record_callback, &rec).has_value());

    // Complete one
    tracker.complete(ids[0], ReturnCode::E_OK, nullptr, 0);

    // Now there's room
    EXPECT_TRUE(tracker.allocate(0x1000, 0x0001, 500, 0,
                                 record_callback, &rec).has_value());
}

// ── evict_expired ───────────────────────────────────────────────

TEST(RequestTracker_EvictExpired, FiresTimeoutCallback) {
    Tracker tracker;
    CallbackRecord rec;

    auto id = tracker.allocate(0x1000, 0x0001, 100, 1000, record_callback, &rec);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(tracker.active_count(), 1u);

    // Time is 1101 → deadline was 1100 → expired
    tracker.evict_expired(1200);

    EXPECT_EQ(rec.call_count, 1);
    EXPECT_EQ(rec.last_rc, ReturnCode::E_TIMEOUT);
    EXPECT_TRUE(rec.last_payload.empty());
    EXPECT_EQ(tracker.active_count(), 0u);
}

TEST(RequestTracker_EvictExpired, NotExpiredYet_NotEvicted) {
    Tracker tracker;
    CallbackRecord rec;

    tracker.allocate(0x1000, 0x0001, 500, 1000, record_callback, &rec);
    tracker.evict_expired(1200); // deadline is 1500, not expired yet

    EXPECT_EQ(rec.call_count, 0);
    EXPECT_EQ(tracker.active_count(), 1u);
}

TEST(RequestTracker_EvictExpired, TimeoutZero_DefaultsToConfigValue) {
    Tracker tracker;
    CallbackRecord rec;

    tracker.allocate(0x1000, 0x0001, 0, 1000, record_callback, &rec);
    // timeout_ms=0 → Config::RequestTimeoutMs=500 → deadline=1500

    tracker.evict_expired(1200); // before deadline
    EXPECT_EQ(rec.call_count, 0);

    tracker.evict_expired(1600); // after deadline
    EXPECT_EQ(rec.call_count, 1);
    EXPECT_EQ(rec.last_rc, ReturnCode::E_TIMEOUT);
}

// ── active_count ────────────────────────────────────────────────

TEST(RequestTracker_ActiveCount, TracksCorrectly) {
    Tracker tracker;
    CallbackRecord rec;
    EXPECT_EQ(tracker.active_count(), 0u);

    auto id1 = tracker.allocate(0x1000, 0x0001, 500, 0, record_callback, &rec);
    EXPECT_EQ(tracker.active_count(), 1u);

    auto id2 = tracker.allocate(0x1000, 0x0002, 500, 0, record_callback, &rec);
    EXPECT_EQ(tracker.active_count(), 2u);

    tracker.complete(*id1, ReturnCode::E_OK, nullptr, 0);
    EXPECT_EQ(tracker.active_count(), 1u);

    tracker.complete(*id2, ReturnCode::E_OK, nullptr, 0);
    EXPECT_EQ(tracker.active_count(), 0u);
}

// ── Null callback ───────────────────────────────────────────────

TEST(RequestTracker_NullCallback, Complete_NoCrash) {
    Tracker tracker;
    auto id = tracker.allocate(0x1000, 0x0001, 500, 0, nullptr, nullptr);
    ASSERT_TRUE(id.has_value());

    // Should not crash with null callback
    EXPECT_TRUE(tracker.complete(*id, ReturnCode::E_OK, nullptr, 0));
}

TEST(RequestTracker_NullCallback, EvictExpired_NoCrash) {
    Tracker tracker;
    tracker.allocate(0x1000, 0x0001, 100, 1000, nullptr, nullptr);
    // Should not crash
    tracker.evict_expired(1200);
    EXPECT_EQ(tracker.active_count(), 0u);
}
