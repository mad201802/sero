/// @file test_e2e_protection.cpp
/// Unit tests for e2e_protection.hpp — sequence counter tracking state machine.

#include <gtest/gtest.h>
#include <sero/security/e2e_protection.hpp>
#include <sero/core/config.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::make_addr;

using E2E = E2EProtection<SmallConfig>;
using Addr = Address<SmallConfig>;

// ── next_sequence ───────────────────────────────────────────────

TEST(E2EProtection_NextSequence, InitialValue_IsZero) {
    E2E e2e;
    EXPECT_EQ(e2e.next_sequence(), 0);
}

TEST(E2EProtection_NextSequence, IncrementsSequentially) {
    E2E e2e;
    EXPECT_EQ(e2e.next_sequence(), 0);
    EXPECT_EQ(e2e.next_sequence(), 1);
    EXPECT_EQ(e2e.next_sequence(), 2);
}

TEST(E2EProtection_NextSequence, WrapsAt255ToZero) {
    E2E e2e;
    // Advance to 255
    for (int i = 0; i < 255; ++i) {
        e2e.next_sequence();
    }
    EXPECT_EQ(e2e.next_sequence(), 255);
    EXPECT_EQ(e2e.next_sequence(), 0); // wraps
    EXPECT_EQ(e2e.next_sequence(), 1);
}

// ── validate_sequence ───────────────────────────────────────────

TEST(E2EProtection_ValidateSequence, FirstSeen_ReturnsFirstSeen) {
    E2E e2e;
    Addr addr = make_addr(1);
    EXPECT_EQ(e2e.validate_sequence(addr, 0), SeqResult::FirstSeen);
}

TEST(E2EProtection_ValidateSequence, Duplicate_ReturnsDuplicate) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 5);  // FirstSeen, records 5
    EXPECT_EQ(e2e.validate_sequence(addr, 5), SeqResult::Duplicate);
}

TEST(E2EProtection_ValidateSequence, WithinWindow_ReturnsAccept) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 0); // FirstSeen

    // Delta 1..15 should be Accept
    EXPECT_EQ(e2e.validate_sequence(addr, 1), SeqResult::Accept);
    EXPECT_EQ(e2e.validate_sequence(addr, 2), SeqResult::Accept);
    EXPECT_EQ(e2e.validate_sequence(addr, 16), SeqResult::Accept); // delta=14 from 2
}

TEST(E2EProtection_ValidateSequence, WindowBoundary_Delta15_Accepts) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 0); // FirstSeen, last_seen=0
    EXPECT_EQ(e2e.validate_sequence(addr, 15), SeqResult::Accept); // delta=15
}

TEST(E2EProtection_ValidateSequence, OutsideWindow_ReturnsStale) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 0); // FirstSeen, last_seen=0
    // delta=16 > SeqCounterAcceptWindow(15) → Stale
    EXPECT_EQ(e2e.validate_sequence(addr, 16), SeqResult::Stale);
}

TEST(E2EProtection_ValidateSequence, WrappingArithmetic_WithinWindow) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 250); // FirstSeen, last_seen=250

    // received=5, delta = (5-250) mod 256 = 11, within window of 15
    EXPECT_EQ(e2e.validate_sequence(addr, 5), SeqResult::Accept);
}

TEST(E2EProtection_ValidateSequence, WrappingArithmetic_OutsideWindow) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 250); // FirstSeen, last_seen=250

    // received=20, delta = (20-250) mod 256 = 26 > 15 → Stale
    EXPECT_EQ(e2e.validate_sequence(addr, 20), SeqResult::Stale);
}

// ── reset_peer ──────────────────────────────────────────────────

TEST(E2EProtection_ResetPeer, ThenResend_ReturnsFirstSeen) {
    E2E e2e;
    Addr addr = make_addr(1);
    e2e.validate_sequence(addr, 5); // FirstSeen
    e2e.validate_sequence(addr, 6); // Accept

    e2e.reset_peer(addr);

    // After reset, same peer should be FirstSeen again
    EXPECT_EQ(e2e.validate_sequence(addr, 0), SeqResult::FirstSeen);
}

TEST(E2EProtection_ResetPeer, NonExistentPeer_NoCrash) {
    E2E e2e;
    Addr addr = make_addr(99);
    // Should not crash
    e2e.reset_peer(addr);
}

TEST(E2EProtection_ResetPeer, OnlyAffectsTargetPeer) {
    E2E e2e;
    Addr addr1 = make_addr(1);
    Addr addr2 = make_addr(2);

    e2e.validate_sequence(addr1, 10); // FirstSeen
    e2e.validate_sequence(addr2, 20); // FirstSeen

    e2e.reset_peer(addr1);

    // addr1 is reset → FirstSeen
    EXPECT_EQ(e2e.validate_sequence(addr1, 0), SeqResult::FirstSeen);
    // addr2 still tracked → Duplicate on same seq
    EXPECT_EQ(e2e.validate_sequence(addr2, 20), SeqResult::Duplicate);
}

// ── Table full ──────────────────────────────────────────────────

TEST(E2EProtection_ValidateSequence, TableFull_ReturnsTableFull) {
    E2E e2e;
    // Fill all MaxTrackedPeers(4) slots
    for (uint8_t i = 0; i < SmallConfig::MaxTrackedPeers; ++i) {
        Addr addr = make_addr(i);
        EXPECT_EQ(e2e.validate_sequence(addr, 0), SeqResult::FirstSeen);
    }

    // One more peer → TableFull
    Addr overflow_addr = make_addr(100);
    EXPECT_EQ(e2e.validate_sequence(overflow_addr, 0), SeqResult::TableFull);
}

// ── Multiple peers independent ──────────────────────────────────

TEST(E2EProtection_ValidateSequence, MultiplePeers_Independent) {
    E2E e2e;
    Addr addr1 = make_addr(1);
    Addr addr2 = make_addr(2);

    e2e.validate_sequence(addr1, 0); // FirstSeen
    e2e.validate_sequence(addr2, 0); // FirstSeen

    // Each peer's sequence counter is tracked independently
    EXPECT_EQ(e2e.validate_sequence(addr1, 1), SeqResult::Accept);
    EXPECT_EQ(e2e.validate_sequence(addr2, 5), SeqResult::Accept);
    EXPECT_EQ(e2e.validate_sequence(addr1, 1), SeqResult::Duplicate);
    EXPECT_EQ(e2e.validate_sequence(addr2, 5), SeqResult::Duplicate);
}
