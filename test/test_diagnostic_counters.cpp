/// @file test_diagnostic_counters.cpp
/// Unit tests for diagnostic_counters.hpp — increment, get, reset, callback.

#include <gtest/gtest.h>
#include <sero/core/diagnostic_counters.hpp>

using namespace sero;

// ── Basic increment and get ─────────────────────────────────────

TEST(DiagnosticCounters_Increment, SingleCounter_IncrementsCorrectly) {
    DiagnosticCounters diag;
    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 0u);

    diag.increment(DiagnosticCounter::CrcErrors);
    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 1u);

    diag.increment(DiagnosticCounter::CrcErrors);
    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 2u);
}

TEST(DiagnosticCounters_Increment, AllCounterTypes_Independent) {
    DiagnosticCounters diag;

    diag.increment(DiagnosticCounter::CrcErrors);
    diag.increment(DiagnosticCounter::VersionMismatches);
    diag.increment(DiagnosticCounter::VersionMismatches);
    diag.increment(DiagnosticCounter::OversizedPayloads);
    diag.increment(DiagnosticCounter::OversizedPayloads);
    diag.increment(DiagnosticCounter::OversizedPayloads);

    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 1u);
    EXPECT_EQ(diag.get(DiagnosticCounter::VersionMismatches), 2u);
    EXPECT_EQ(diag.get(DiagnosticCounter::OversizedPayloads), 3u);
    EXPECT_EQ(diag.get(DiagnosticCounter::TypeIdMismatches), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::DuplicateMessages), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::StaleMessages), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::AuthFailures), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::UnknownMessageTypes), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::DroppedMessages), 0u);
}

// ── Reset ───────────────────────────────────────────────────────

TEST(DiagnosticCounters_Reset, ZerosAllCounters) {
    DiagnosticCounters diag;
    diag.increment(DiagnosticCounter::CrcErrors);
    diag.increment(DiagnosticCounter::AuthFailures);
    diag.increment(DiagnosticCounter::DroppedMessages);

    diag.reset();

    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::AuthFailures), 0u);
    EXPECT_EQ(diag.get(DiagnosticCounter::DroppedMessages), 0u);
}

// ── Callback ────────────────────────────────────────────────────

namespace {

struct CallbackState {
    int call_count = 0;
    DiagnosticCounter last_counter = DiagnosticCounter::_Count;
    const uint8_t* last_header = nullptr;
};

void test_callback(DiagnosticCounter counter, const uint8_t* header, void* user_ctx) {
    auto* state = static_cast<CallbackState*>(user_ctx);
    ++state->call_count;
    state->last_counter = counter;
    state->last_header = header;
}

} // namespace

TEST(DiagnosticCounters_Callback, InvokedOnIncrement) {
    DiagnosticCounters diag;
    CallbackState state;
    diag.set_callback(test_callback, &state);

    uint8_t fake_header[20] = {0x01};
    diag.increment(DiagnosticCounter::CrcErrors, fake_header);

    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.last_counter, DiagnosticCounter::CrcErrors);
    EXPECT_EQ(state.last_header, fake_header);
}

TEST(DiagnosticCounters_Callback, NullHeader_NoCrash) {
    DiagnosticCounters diag;
    CallbackState state;
    diag.set_callback(test_callback, &state);

    diag.increment(DiagnosticCounter::DroppedMessages, nullptr);

    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.last_header, nullptr);
}

TEST(DiagnosticCounters_Callback, NoCallback_NoCrash) {
    DiagnosticCounters diag;
    // No callback set — should not crash
    diag.increment(DiagnosticCounter::CrcErrors);
    EXPECT_EQ(diag.get(DiagnosticCounter::CrcErrors), 1u);
}

TEST(DiagnosticCounters_Callback, MultipleIncrements_CallbackCalledEachTime) {
    DiagnosticCounters diag;
    CallbackState state;
    diag.set_callback(test_callback, &state);

    diag.increment(DiagnosticCounter::CrcErrors);
    diag.increment(DiagnosticCounter::AuthFailures);
    diag.increment(DiagnosticCounter::DroppedMessages);

    EXPECT_EQ(state.call_count, 3);
    EXPECT_EQ(state.last_counter, DiagnosticCounter::DroppedMessages);
}

// ── Default state ───────────────────────────────────────────────

TEST(DiagnosticCounters_DefaultState, AllCountersZero) {
    DiagnosticCounters diag;
    for (uint8_t i = 0; i < static_cast<uint8_t>(DiagnosticCounter::_Count); ++i) {
        EXPECT_EQ(diag.get(static_cast<DiagnosticCounter>(i)), 0u);
    }
}
