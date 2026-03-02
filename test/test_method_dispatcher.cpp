/// @file test_method_dispatcher.cpp
/// Unit tests for method_dispatcher.hpp — service registration, dispatch, lookup.

#include <algorithm>

#include <gtest/gtest.h>
#include <sero/service/method_dispatcher.hpp>
#include <sero/service/service.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::StubService;

using Dispatcher = MethodDispatcher<SmallConfig>;

// Helper to create a service entry from a StubService
ServiceEntry make_stub_entry(uint16_t service_id, StubService& svc,
                             uint8_t major = 1, uint8_t minor = 0,
                             bool auth = false) {
    return make_service_entry(service_id, svc, major, minor, auth);
}

// ── register_service ────────────────────────────────────────────

TEST(MethodDispatcher_RegisterService, Success) {
    Dispatcher disp;
    StubService svc;
    EXPECT_TRUE(disp.register_service(make_stub_entry(0x1000, svc)));
    EXPECT_EQ(disp.count(), 1u);
}

TEST(MethodDispatcher_RegisterService, DuplicateId_ReturnsFalse) {
    Dispatcher disp;
    StubService svc1, svc2;
    EXPECT_TRUE(disp.register_service(make_stub_entry(0x1000, svc1)));
    EXPECT_FALSE(disp.register_service(make_stub_entry(0x1000, svc2)));
    EXPECT_EQ(disp.count(), 1u);
}

TEST(MethodDispatcher_RegisterService, TableFull_ReturnsFalse) {
    Dispatcher disp;
    StubService svcs[SmallConfig::MaxServices + 1];

    for (std::size_t i = 0; i < SmallConfig::MaxServices; ++i) {
        EXPECT_TRUE(disp.register_service(
            make_stub_entry(static_cast<uint16_t>(0x1000 + i), svcs[i])));
    }
    // One more → table full
    EXPECT_FALSE(disp.register_service(
        make_stub_entry(0x2000, svcs[SmallConfig::MaxServices])));
}

// ── unregister_service ──────────────────────────────────────────

TEST(MethodDispatcher_UnregisterService, Found_ReturnsTrue) {
    Dispatcher disp;
    StubService svc;
    disp.register_service(make_stub_entry(0x1000, svc));
    EXPECT_TRUE(disp.unregister_service(0x1000));
    EXPECT_EQ(disp.count(), 0u);
}

TEST(MethodDispatcher_UnregisterService, NotFound_ReturnsFalse) {
    Dispatcher disp;
    EXPECT_FALSE(disp.unregister_service(0x9999));
}

TEST(MethodDispatcher_UnregisterService, FreesSlotForReuse) {
    Dispatcher disp;
    StubService svcs[SmallConfig::MaxServices + 1];

    // Fill table
    for (std::size_t i = 0; i < SmallConfig::MaxServices; ++i) {
        disp.register_service(
            make_stub_entry(static_cast<uint16_t>(0x1000 + i), svcs[i]));
    }
    // Unregister one
    EXPECT_TRUE(disp.unregister_service(0x1000));
    // Now there's room
    EXPECT_TRUE(disp.register_service(
        make_stub_entry(0x2000, svcs[SmallConfig::MaxServices])));
}

// ── dispatch ────────────────────────────────────────────────────

TEST(MethodDispatcher_Dispatch, UnknownService_ReturnsUnknownService) {
    Dispatcher disp;
    uint8_t response[64];
    std::size_t resp_len = sizeof(response);
    ReturnCode rc = disp.dispatch(0x9999, 0x0001, nullptr, 0,
                                  response, resp_len);
    EXPECT_EQ(rc, ReturnCode::E_UNKNOWN_SERVICE);
}

TEST(MethodDispatcher_Dispatch, ServiceNotReady_ReturnsNotReady) {
    Dispatcher disp;
    StubService svc;
    svc.ready = false;
    disp.register_service(make_stub_entry(0x1000, svc));

    uint8_t response[64];
    std::size_t resp_len = sizeof(response);
    ReturnCode rc = disp.dispatch(0x1000, 0x0001, nullptr, 0,
                                  response, resp_len);
    EXPECT_EQ(rc, ReturnCode::E_NOT_READY);
}

TEST(MethodDispatcher_Dispatch, DelegatesToHandler_ReturnsOk) {
    Dispatcher disp;
    StubService svc;
    svc.response_data = {0xAA, 0xBB, 0xCC};
    disp.register_service(make_stub_entry(0x1000, svc));

    const uint8_t payload[] = {0x01, 0x02};
    uint8_t response[64];
    std::size_t resp_len = sizeof(response);
    ReturnCode rc = disp.dispatch(0x1000, 0x0042, payload, 2,
                                  response, resp_len);

    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(svc.last_method_id, 0x0042);
    EXPECT_EQ(svc.last_payload.size(), 2u);
    EXPECT_EQ(svc.last_payload[0], 0x01);
    EXPECT_EQ(resp_len, 3u);
    EXPECT_EQ(response[0], 0xAA);
    EXPECT_EQ(response[1], 0xBB);
    EXPECT_EQ(response[2], 0xCC);
}

TEST(MethodDispatcher_Dispatch, HandlerReturnsError) {
    Dispatcher disp;
    StubService svc;
    svc.return_code = ReturnCode::E_UNKNOWN_METHOD;
    disp.register_service(make_stub_entry(0x1000, svc));

    uint8_t response[64];
    std::size_t resp_len = sizeof(response);
    ReturnCode rc = disp.dispatch(0x1000, 0x0001, nullptr, 0,
                                  response, resp_len);
    EXPECT_EQ(rc, ReturnCode::E_UNKNOWN_METHOD);
}

// ── find ────────────────────────────────────────────────────────

TEST(MethodDispatcher_Find, KnownService_ReturnsEntry) {
    Dispatcher disp;
    StubService svc;
    disp.register_service(make_stub_entry(0x1000, svc, 2, 5, true));

    const ServiceEntry* entry = disp.find(0x1000);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->service_id, 0x1000);
    EXPECT_EQ(entry->major_version, 2);
    EXPECT_EQ(entry->minor_version, 5);
    EXPECT_TRUE(entry->auth_required);
}

TEST(MethodDispatcher_Find, UnknownService_ReturnsNullptr) {
    Dispatcher disp;
    EXPECT_EQ(disp.find(0x9999), nullptr);
}

// ── for_each ────────────────────────────────────────────────────

TEST(MethodDispatcher_ForEach, IteratesActiveServices) {
    Dispatcher disp;
    StubService svc1, svc2;
    disp.register_service(make_stub_entry(0x1000, svc1));
    disp.register_service(make_stub_entry(0x2000, svc2));

    std::vector<uint16_t> ids;
    disp.for_each([&](const ServiceEntry& e) {
        ids.push_back(e.service_id);
    });

    EXPECT_EQ(ids.size(), 2u);
    // Order doesn't matter
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), uint16_t(0x1000)) != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), uint16_t(0x2000)) != ids.end());
}

// ── count ───────────────────────────────────────────────────────

TEST(MethodDispatcher_Count, TracksCorrectly) {
    Dispatcher disp;
    StubService svc1, svc2;
    EXPECT_EQ(disp.count(), 0u);
    disp.register_service(make_stub_entry(0x1000, svc1));
    EXPECT_EQ(disp.count(), 1u);
    disp.register_service(make_stub_entry(0x2000, svc2));
    EXPECT_EQ(disp.count(), 2u);
    disp.unregister_service(0x1000);
    EXPECT_EQ(disp.count(), 1u);
}
