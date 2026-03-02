/// @file test_service_event_handler.cpp
/// Unit tests for service.hpp and event_handler.hpp — CRTP + type erasure correctness.

#include <gtest/gtest.h>
#include <sero/service/service.hpp>
#include <sero/service/event_handler.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::StubService;
using test::StubEventHandler;

// ── IService CRTP + make_service_entry ──────────────────────────

TEST(ServiceEntry_MakeServiceEntry, FieldsCorrectlySet) {
    StubService svc;
    ServiceEntry entry = make_service_entry(0x1234, svc, 2, 5, true);

    EXPECT_EQ(entry.service_id, 0x1234);
    EXPECT_EQ(entry.major_version, 2);
    EXPECT_EQ(entry.minor_version, 5);
    EXPECT_TRUE(entry.auth_required);
    EXPECT_TRUE(entry.active);
    EXPECT_NE(entry.context, nullptr);
    EXPECT_NE(entry.on_request_fn, nullptr);
    EXPECT_NE(entry.is_ready_fn, nullptr);
}

TEST(ServiceEntry_MakeServiceEntry, DispatchesOnRequestCorrectly) {
    StubService svc;
    svc.response_data = {0xAA, 0xBB};
    ServiceEntry entry = make_service_entry(0x1234, svc, 1, 0);

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t response[64];
    std::size_t resp_len = sizeof(response);

    ReturnCode rc = entry.on_request_fn(entry.context, 0x0042,
                                         payload, 3, response, resp_len);

    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(svc.last_method_id, 0x0042);
    EXPECT_EQ(svc.last_payload.size(), 3u);
    EXPECT_EQ(resp_len, 2u);
    EXPECT_EQ(response[0], 0xAA);
    EXPECT_EQ(response[1], 0xBB);
}

TEST(ServiceEntry_MakeServiceEntry, DispatchesIsReadyCorrectly) {
    StubService svc;
    svc.ready = true;
    ServiceEntry entry = make_service_entry(0x1234, svc, 1, 0);
    EXPECT_TRUE(entry.is_ready_fn(entry.context));

    svc.ready = false;
    EXPECT_FALSE(entry.is_ready_fn(entry.context));
}

TEST(ServiceEntry_MakeServiceEntry, AuthRequiredDefault_IsFalse) {
    StubService svc;
    ServiceEntry entry = make_service_entry(0x1234, svc, 1, 0);
    EXPECT_FALSE(entry.auth_required);
}

// ── IService CRTP dispatch ──────────────────────────────────────

TEST(IService_OnRequest, CrtpDispatchWorks) {
    StubService svc;
    svc.return_code = ReturnCode::E_UNKNOWN_METHOD;

    uint8_t response[64];
    std::size_t resp_len = sizeof(response);
    ReturnCode rc = svc.on_request(0x0001, nullptr, 0, response, resp_len);
    EXPECT_EQ(rc, ReturnCode::E_UNKNOWN_METHOD);
}

TEST(IService_IsReady, CrtpDispatchWorks) {
    StubService svc;
    svc.ready = false;
    EXPECT_FALSE(svc.is_ready());
    svc.ready = true;
    EXPECT_TRUE(svc.is_ready());
}

// ── IEventHandler CRTP + make_event_handler_entry ───────────────

TEST(EventHandlerEntry_MakeEventHandlerEntry, FieldsCorrectlySet) {
    StubEventHandler handler;
    EventHandlerEntry entry = make_event_handler_entry(0x1000, 0x8001, handler);

    EXPECT_EQ(entry.service_id, 0x1000);
    EXPECT_EQ(entry.event_id, 0x8001);
    EXPECT_TRUE(entry.active);
    EXPECT_NE(entry.context, nullptr);
    EXPECT_NE(entry.on_event_fn, nullptr);
}

TEST(EventHandlerEntry_MakeEventHandlerEntry, DispatchesOnEventCorrectly) {
    StubEventHandler handler;
    EventHandlerEntry entry = make_event_handler_entry(0x1000, 0x8001, handler);

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    entry.on_event_fn(entry.context, 0x1000, 0x8001, payload, 4);

    ASSERT_EQ(handler.events.size(), 1u);
    EXPECT_EQ(handler.events[0].service_id, 0x1000);
    EXPECT_EQ(handler.events[0].event_id, 0x8001);
    EXPECT_EQ(handler.events[0].payload.size(), 4u);
    EXPECT_EQ(handler.events[0].payload[0], 0xDE);
}

// ── IEventHandler CRTP dispatch ─────────────────────────────────

TEST(IEventHandler_OnEvent, CrtpDispatchWorks) {
    StubEventHandler handler;
    const uint8_t payload[] = {0x42};
    handler.on_event(0x2000, 0x8002, payload, 1);

    ASSERT_EQ(handler.events.size(), 1u);
    EXPECT_EQ(handler.events[0].service_id, 0x2000);
    EXPECT_EQ(handler.events[0].event_id, 0x8002);
}
