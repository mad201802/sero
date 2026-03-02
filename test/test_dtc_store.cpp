/// @file test_dtc_store.cpp
/// Unit tests for DtcStore (§10.1).

#include <gtest/gtest.h>
#include "test_helpers.hpp"

using namespace sero;

// ── Report single DTC ───────────────────────────────────────────

TEST(DtcStore, ReportSingleDtc) {
    DtcStore<test::SmallConfig> store;
    EXPECT_EQ(store.count(), 0u);

    bool ok = store.report(0x0010, DtcSeverity::Error, 1000);
    EXPECT_TRUE(ok);
    EXPECT_EQ(store.count(), 1u);

    const Dtc* d = store.find(0x0010);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->code, 0x0010);
    EXPECT_EQ(d->severity, static_cast<uint8_t>(DtcSeverity::Error));
    EXPECT_TRUE(d->active);
    EXPECT_EQ(d->occurrence_count, 1u);
    EXPECT_EQ(d->first_seen_ms, 1000u);
    EXPECT_EQ(d->last_seen_ms, 1000u);
}

// ── Report same code twice → increments ─────────────────────────

TEST(DtcStore, ReReportIncrements) {
    DtcStore<test::SmallConfig> store;
    store.report(0x0010, DtcSeverity::Warning, 1000);
    store.report(0x0010, DtcSeverity::Error, 2000);

    EXPECT_EQ(store.count(), 1u);

    const Dtc* d = store.find(0x0010);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->occurrence_count, 2u);
    EXPECT_EQ(d->first_seen_ms, 1000u);
    EXPECT_EQ(d->last_seen_ms, 2000u);
    // Severity updated to the latest
    EXPECT_EQ(d->severity, static_cast<uint8_t>(DtcSeverity::Error));
}

// ── Table full → graceful drop ──────────────────────────────────

TEST(DtcStore, TableFullDrops) {
    DtcStore<test::SmallConfig> store;  // MaxDtcs = 4

    for (uint16_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(store.report(i + 1, DtcSeverity::Info, 100));
    }
    EXPECT_EQ(store.count(), 4u);

    // 5th should fail
    EXPECT_FALSE(store.report(0x0099, DtcSeverity::Fatal, 200));
    EXPECT_EQ(store.count(), 4u);
    EXPECT_EQ(store.find(0x0099), nullptr);
}

// ── Clear single DTC ────────────────────────────────────────────

TEST(DtcStore, ClearSingle) {
    DtcStore<test::SmallConfig> store;
    store.report(0x0001, DtcSeverity::Warning, 100);
    store.report(0x0002, DtcSeverity::Error, 200);
    EXPECT_EQ(store.count(), 2u);

    EXPECT_TRUE(store.clear(0x0001));
    EXPECT_EQ(store.count(), 1u);
    EXPECT_EQ(store.find(0x0001), nullptr);
    EXPECT_NE(store.find(0x0002), nullptr);
}

// ── Clear non-existent returns false ────────────────────────────

TEST(DtcStore, ClearNonExistent) {
    DtcStore<test::SmallConfig> store;
    EXPECT_FALSE(store.clear(0x9999));
}

// ── Clear all ───────────────────────────────────────────────────

TEST(DtcStore, ClearAll) {
    DtcStore<test::SmallConfig> store;
    store.report(0x0001, DtcSeverity::Info, 100);
    store.report(0x0002, DtcSeverity::Warning, 200);
    store.report(0x0003, DtcSeverity::Error, 300);

    store.clear_all();
    EXPECT_EQ(store.count(), 0u);
    EXPECT_EQ(store.find(0x0001), nullptr);
    EXPECT_EQ(store.find(0x0002), nullptr);
    EXPECT_EQ(store.find(0x0003), nullptr);
}

// ── for_each iteration ──────────────────────────────────────────

TEST(DtcStore, ForEachIteratesAll) {
    DtcStore<test::SmallConfig> store;
    store.report(0x0010, DtcSeverity::Error, 100);
    store.report(0x0020, DtcSeverity::Warning, 200);
    store.report(0x0030, DtcSeverity::Info, 300);

    std::size_t visited = 0;
    store.for_each([&](const Dtc& d) {
        EXPECT_TRUE(d.active);
        ++visited;
    });
    EXPECT_EQ(visited, 3u);
}

// ── Report after clear reuses slot ──────────────────────────────

TEST(DtcStore, ReportAfterClear) {
    DtcStore<test::SmallConfig> store;  // MaxDtcs = 4
    store.report(0x0001, DtcSeverity::Info, 100);
    store.report(0x0002, DtcSeverity::Info, 100);
    store.report(0x0003, DtcSeverity::Info, 100);
    store.report(0x0004, DtcSeverity::Info, 100);

    // Full — clear one, then add should succeed
    store.clear(0x0002);
    EXPECT_EQ(store.count(), 3u);

    EXPECT_TRUE(store.report(0x0005, DtcSeverity::Fatal, 500));
    EXPECT_EQ(store.count(), 4u);
    EXPECT_NE(store.find(0x0005), nullptr);
}

// ── Find returns nullptr for empty store ────────────────────────

TEST(DtcStore, FindEmptyStore) {
    DtcStore<test::SmallConfig> store;
    EXPECT_EQ(store.find(0x0001), nullptr);
}

// ── Multiple severities are independent ─────────────────────────

TEST(DtcStore, MultipleDtcIndependent) {
    DtcStore<test::SmallConfig> store;
    store.report(0x0001, DtcSeverity::Info, 100);
    store.report(0x0002, DtcSeverity::Fatal, 200);

    const Dtc* d1 = store.find(0x0001);
    const Dtc* d2 = store.find(0x0002);
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(d1->severity, static_cast<uint8_t>(DtcSeverity::Info));
    EXPECT_EQ(d2->severity, static_cast<uint8_t>(DtcSeverity::Fatal));
    EXPECT_EQ(d1->occurrence_count, 1u);
    EXPECT_EQ(d2->occurrence_count, 1u);
}
