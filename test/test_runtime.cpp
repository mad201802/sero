/// @file test_runtime.cpp
/// P3 Integration tests for Runtime — full validation pipeline and message flows.

#include <gtest/gtest.h>
#include <sero/runtime.hpp>

#include "test_helpers.hpp"

using namespace sero;
using test::SmallConfig;
using test::MockTransport;
using test::StubService;
using test::StubEventHandler;
using test::build_raw_message;
using test::make_addr;
using test::Addr;

using RT = Runtime<MockTransport, SmallConfig>;

// ── Fixture ─────────────────────────────────────────────────────

class RuntimeTest : public ::testing::Test {
protected:
    MockTransport transport;
    std::unique_ptr<RT> rt;

    void SetUp() override {
        transport.clear();
        rt = std::make_unique<RT>(transport, /*client_id=*/0x0001);
        rt->set_local_address(make_addr(1));
    }

    /// Build a valid message header, inject it, and call process().
    void inject_and_process(const MessageHeader& hdr,
                            const uint8_t* payload = nullptr,
                            std::size_t payload_length = 0,
                            const Addr& source = make_addr(10),
                            uint32_t now_ms = 0,
                            const uint8_t* hmac = nullptr) {
        auto raw = build_raw_message(hdr, payload, payload_length, hmac);
        transport.receive_queue.push_back({source, std::move(raw)});
        rt->process(now_ms);
    }

    /// Make a basic valid REQUEST header (no auth).
    MessageHeader make_request(uint16_t service_id = 0x1000,
                               uint16_t method_id = 0x0001,
                               uint16_t payload_len = 0,
                               uint32_t request_id = 1) {
        MessageHeader hdr{};
        hdr.version          = PROTOCOL_VERSION;
        hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST);
        hdr.return_code      = 0;
        hdr.flags            = 0;
        hdr.service_id       = service_id;
        hdr.method_event_id  = method_id;
        hdr.client_id        = 0x0002; // some remote client
        hdr.sequence_counter = 0;
        hdr.request_id       = request_id;
        hdr.payload_length   = payload_len;
        return hdr;
    }

    /// Make a basic RESPONSE header.
    MessageHeader make_response(uint16_t service_id = 0x1000,
                                uint16_t method_id = 0x0001,
                                uint16_t payload_len = 0,
                                uint32_t request_id = 1,
                                ReturnCode rc = ReturnCode::E_OK) {
        MessageHeader hdr{};
        hdr.version          = PROTOCOL_VERSION;
        hdr.message_type     = static_cast<uint8_t>(MessageType::RESPONSE);
        hdr.return_code      = static_cast<uint8_t>(rc);
        hdr.flags            = 0;
        hdr.service_id       = service_id;
        hdr.method_event_id  = method_id;
        hdr.client_id        = 0x0000; // provider
        hdr.sequence_counter = 0;
        hdr.request_id       = request_id;
        hdr.payload_length   = payload_len;
        return hdr;
    }

    /// Make a NOTIFICATION header (event_id has high bit set).
    MessageHeader make_notification(uint16_t service_id = 0x1000,
                                    uint16_t event_id = 0x8001,
                                    uint16_t payload_len = 0) {
        MessageHeader hdr{};
        hdr.version          = PROTOCOL_VERSION;
        hdr.message_type     = static_cast<uint8_t>(MessageType::NOTIFICATION);
        hdr.return_code      = 0;
        hdr.flags            = 0;
        hdr.service_id       = service_id;
        hdr.method_event_id  = event_id;
        hdr.client_id        = 0x0000;
        hdr.sequence_counter = 0;
        hdr.request_id       = REQUEST_ID_NONE;
        hdr.payload_length   = payload_len;
        return hdr;
    }
};

// ── Validation Step 1: Minimum size ─────────────────────────────

TEST_F(RuntimeTest, Validation_TooShort_DropsMessage) {
    std::vector<uint8_t> tiny = {0x01, 0x02};
    transport.receive_queue.push_back({make_addr(10), tiny});
    rt->process(0);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::DroppedMessages), 1u);
    EXPECT_TRUE(transport.sent.empty());
}

// ── Validation Step 2: CRC error ────────────────────────────────

TEST_F(RuntimeTest, Validation_BadCrc_IncrementsCounter) {
    auto raw = build_raw_message(make_request());
    // Corrupt the last byte (CRC)
    raw.back() ^= 0xFF;
    transport.receive_queue.push_back({make_addr(10), raw});
    rt->process(0);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::CrcErrors), 1u);
}

// ── Validation Step 3: Wrong protocol version ───────────────────

TEST_F(RuntimeTest, Validation_BadVersion_IncrementsCounter) {
    MessageHeader hdr = make_request();
    hdr.version = 0xFF; // wrong
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::VersionMismatches), 1u);
}

// ── Validation Step 4: Oversized payload ────────────────────────

TEST_F(RuntimeTest, Validation_OversizedPayload_IncrementsCounter) {
    MessageHeader hdr = make_request();
    hdr.payload_length = SmallConfig::MaxPayloadSize + 100; // exceeds limit
    // We still need a valid CRC on the header, so use build_raw_message
    // but notice the actual payload won't be that long—header declares it oversized
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::OversizedPayloads), 1u);
}

// ── Validation Step 5: Unknown message type ─────────────────────

TEST_F(RuntimeTest, Validation_UnknownMessageType_IncrementsCounter) {
    MessageHeader hdr = make_request();
    hdr.message_type = 0xEE; // invalid
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::UnknownMessageTypes), 1u);
}

// ── Validation Step 6: Type/ID mismatch ─────────────────────────

TEST_F(RuntimeTest, Validation_TypeIdMismatch_IncrementsCounter) {
    MessageHeader hdr{};
    hdr.version          = PROTOCOL_VERSION;
    hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.return_code      = 0;
    hdr.flags            = 0;
    hdr.service_id       = 0x1000;
    hdr.method_event_id  = 0x8001; // event ID with REQUEST → mismatch
    hdr.client_id        = 0x0002;
    hdr.sequence_counter = 0;
    hdr.request_id       = 1;
    hdr.payload_length   = 0;
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::TypeIdMismatches), 1u);
}

// ── Validation Step 7: Client ID ────────────────────────────────

TEST_F(RuntimeTest, Validation_RequestWithZeroClientId_Drops) {
    MessageHeader hdr = make_request();
    hdr.client_id = 0; // Invalid for REQUEST
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::DroppedMessages), 1u);
}

// ── Validation Step 8: Duplicate sequence counter ───────────────

TEST_F(RuntimeTest, Validation_DuplicateSequence_IncrementsCounter) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    // First message with seq=5 → FirstSeen, accepted
    MessageHeader hdr1 = make_request(0x1000, 0x0001, 0, 1);
    hdr1.sequence_counter = 5;
    inject_and_process(hdr1, nullptr, 0, make_addr(10), 0);

    // Second message from same source with same seq=5 → Duplicate
    MessageHeader hdr2 = make_request(0x1000, 0x0001, 0, 2);
    hdr2.sequence_counter = 5;
    inject_and_process(hdr2, nullptr, 0, make_addr(10), 100);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::DuplicateMessages), 1u);
}

// ── Validation Step 9: HMAC verification ────────────────────────

TEST_F(RuntimeTest, Validation_AuthRequired_NoHmac_Drops) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0, /*auth_required=*/true);

    // Message without auth flag → auth failure
    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    hdr.flags = 0; // no auth
    inject_and_process(hdr);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::AuthFailures), 1u);
}

TEST_F(RuntimeTest, Validation_AuthPresentButBadHmac_Drops) {
    Addr peer = make_addr(10);
    uint8_t key[SmallConfig::HmacKeySize] = {};
    std::memset(key, 0xAA, sizeof(key));
    rt->set_hmac_key(peer, key);

    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    hdr.flags = FLAG_AUTH;

    // Build with bad HMAC
    uint8_t bad_hmac[HMAC_SIZE] = {};
    std::memset(bad_hmac, 0xFF, HMAC_SIZE);
    inject_and_process(hdr, nullptr, 0, peer, 0, bad_hmac);

    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::AuthFailures), 1u);
}

// ── Request/Response flow ───────────────────────────────────────

TEST_F(RuntimeTest, RequestResponse_FullFlow) {
    StubService svc;
    svc.response_data = {0xCA, 0xFE};
    rt->register_service(0x1000, svc, 1, 0);

    uint8_t payload[] = {0x01, 0x02};
    MessageHeader hdr = make_request(0x1000, 0x0042, 2, 100);
    inject_and_process(hdr, payload, 2, make_addr(10), 0);

    // Service was called
    EXPECT_EQ(svc.last_method_id, 0x0042);
    EXPECT_EQ(svc.last_payload.size(), 2u);

    // Response sent back
    ASSERT_EQ(transport.sent.size(), 1u);
    EXPECT_EQ(transport.sent[0].destination, make_addr(10));

    // Parse response header
    auto& resp_data = transport.sent[0].data;
    ASSERT_GE(resp_data.size(), HEADER_SIZE);
    MessageHeader resp_hdr = MessageHeader::deserialize(resp_data.data());
    EXPECT_EQ(resp_hdr.message_type,
              static_cast<uint8_t>(MessageType::RESPONSE));
    EXPECT_EQ(resp_hdr.return_code, static_cast<uint8_t>(ReturnCode::E_OK));
    EXPECT_EQ(resp_hdr.request_id, 100u);
    EXPECT_EQ(resp_hdr.payload_length, 2u);
}

TEST_F(RuntimeTest, RequestResponse_UnknownService_SendsError) {
    // No service registered for 0x1000
    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    inject_and_process(hdr, nullptr, 0, make_addr(10), 0);

    ASSERT_EQ(transport.sent.size(), 1u);
    MessageHeader resp_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(resp_hdr.message_type, static_cast<uint8_t>(MessageType::ERROR));
    EXPECT_EQ(resp_hdr.return_code,
              static_cast<uint8_t>(ReturnCode::E_UNKNOWN_SERVICE));
}

TEST_F(RuntimeTest, RequestResponse_NotReady_SendsError) {
    StubService svc;
    svc.ready = false;
    rt->register_service(0x1000, svc, 1, 0);

    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    inject_and_process(hdr, nullptr, 0, make_addr(10), 0);

    ASSERT_EQ(transport.sent.size(), 1u);
    MessageHeader resp_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(resp_hdr.message_type, static_cast<uint8_t>(MessageType::ERROR));
    EXPECT_EQ(resp_hdr.return_code,
              static_cast<uint8_t>(ReturnCode::E_NOT_READY));
}

// ── Fire-and-Forget ─────────────────────────────────────────────

TEST_F(RuntimeTest, FireAndForget_Dispatches_NoResponse) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    MessageHeader hdr{};
    hdr.version          = PROTOCOL_VERSION;
    hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    hdr.return_code      = 0;
    hdr.flags            = 0;
    hdr.service_id       = 0x1000;
    hdr.method_event_id  = 0x0042;
    hdr.client_id        = 0x0002;
    hdr.sequence_counter = 0;
    hdr.request_id       = REQUEST_ID_NONE;
    hdr.payload_length   = 0;
    inject_and_process(hdr, nullptr, 0, make_addr(10), 0);

    EXPECT_EQ(svc.last_method_id, 0x0042);
    EXPECT_TRUE(transport.sent.empty()); // no response
}

// ── Client-side Request ─────────────────────────────────────────

TEST_F(RuntimeTest, ClientRequest_ServiceNotFound_ReturnsNullopt) {
    auto rid = rt->request(0x1000, 0x0001, nullptr, 0,
                           nullptr, nullptr, 500, 0);
    EXPECT_FALSE(rid.has_value());
}

TEST_F(RuntimeTest, ClientRequest_ServiceFound_SendsAndTracksRequest) {
    // Emulate consumer-side: find → handle_offer → provider known
    rt->find_service(0x1000, 1, 0);
    rt->service_discovery().handle_offer(0x1000, 1, 30, 0x0001, make_addr(20), 0);

    bool callback_called = false;
    auto cb = [](ReturnCode rc, const uint8_t*, std::size_t, void* ctx) {
        *static_cast<bool*>(ctx) = true;
        (void)rc;
    };

    auto rid = rt->request(0x1000, 0x0001, nullptr, 0,
                           cb, &callback_called, 500, 100);
    ASSERT_TRUE(rid.has_value());
    EXPECT_EQ(transport.sent.size(), 1u);

    // Inject a RESPONSE for this request_id
    uint8_t resp_payload[] = {0xAA};
    MessageHeader resp_hdr = make_response(0x1000, 0x0001, 1, *rid);
    inject_and_process(resp_hdr, resp_payload, 1, make_addr(20), 200);

    EXPECT_TRUE(callback_called);
}

// ── Client-side Explicit-Target Request ─────────────────────────

TEST_F(RuntimeTest, ClientRequest_ExplicitTarget_SendsWithoutSD) {
    // SD has no provider registered — explicit-target overload must still send
    Addr target = make_addr(42);

    bool callback_called = false;
    auto cb = [](ReturnCode, const uint8_t*, std::size_t, void* ctx) {
        *static_cast<bool*>(ctx) = true;
    };

    auto rid = rt->request(target, 0x1000, 0x0001, nullptr, 0,
                           cb, &callback_called, 500, 0);
    ASSERT_TRUE(rid.has_value());
    ASSERT_EQ(transport.sent.size(), 1u);

    // Verify the message was sent to the explicit target
    EXPECT_EQ(transport.sent[0].destination, target);

    MessageHeader sent_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(sent_hdr.message_type, static_cast<uint8_t>(MessageType::REQUEST));
    EXPECT_EQ(sent_hdr.service_id, 0x1000);
    EXPECT_EQ(sent_hdr.method_event_id, 0x0001);
}

TEST_F(RuntimeTest, ClientRequest_ExplicitTarget_ResponseCompletesCallback) {
    Addr target = make_addr(42);

    bool callback_called = false;
    ReturnCode received_rc = ReturnCode::E_NOT_OK;
    auto cb = [](ReturnCode rc, const uint8_t*, std::size_t, void* ctx) {
        auto* pair = static_cast<std::pair<bool*, ReturnCode*>*>(ctx);
        *pair->first  = true;
        *pair->second = rc;
    };

    std::pair<bool*, ReturnCode*> ctx{&callback_called, &received_rc};
    auto rid = rt->request(target, 0x1000, 0x0001, nullptr, 0,
                           cb, &ctx, 500, 100);
    ASSERT_TRUE(rid.has_value());

    // Inject a RESPONSE for this request_id from the explicit target
    MessageHeader resp_hdr = make_response(0x1000, 0x0001, 0, *rid);
    inject_and_process(resp_hdr, nullptr, 0, target, 200);

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_rc, ReturnCode::E_OK);
}

// ── Client-side Fire-and-Forget ─────────────────────────────────

TEST_F(RuntimeTest, ClientFireAndForget_ServiceFound_Sends) {
    rt->find_service(0x1000, 1, 0);
    rt->service_discovery().handle_offer(0x1000, 1, 30, 0x0001, make_addr(20), 0);

    bool ok = rt->fire_and_forget(0x1000, 0x0042, nullptr, 0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(transport.sent.size(), 1u);

    MessageHeader sent_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(sent_hdr.message_type,
              static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN));
    EXPECT_EQ(sent_hdr.method_event_id, 0x0042);
}

TEST_F(RuntimeTest, ClientFireAndForget_ServiceNotFound_ReturnsFalse) {
    EXPECT_FALSE(rt->fire_and_forget(0x1000, 0x0042, nullptr, 0));
}

// ── Notification (provider side) ────────────────────────────────

TEST_F(RuntimeTest, NotifyEvent_NoSubscribers_ReturnsFalse) {
    rt->register_event(0x1000, 0x8001);
    EXPECT_FALSE(rt->notify_event(0x1000, 0x8001, nullptr, 0));
}

// ── Notification (consumer side) ────────────────────────────────

TEST_F(RuntimeTest, NotificationDispatch_MatchingHandler) {
    StubEventHandler handler;
    // Register a service so we can subscribe
    rt->find_service(0x1000, 1, 0);
    rt->service_discovery().handle_offer(0x1000, 1, 30, 0x0001, make_addr(20), 0);
    rt->subscribe_event(0x1000, 0x8001, handler, 60, 0);

    // Inject a NOTIFICATION
    uint8_t payload[] = {0xDE, 0xAD};
    MessageHeader hdr = make_notification(0x1000, 0x8001, 2);
    inject_and_process(hdr, payload, 2, make_addr(20), 100);

    ASSERT_EQ(handler.events.size(), 1u);
    EXPECT_EQ(handler.events[0].service_id, 0x1000);
    EXPECT_EQ(handler.events[0].event_id, 0x8001);
    EXPECT_EQ(handler.events[0].payload.size(), 2u);
}

TEST_F(RuntimeTest, NotificationDispatch_NoHandler_Ignored) {
    // Inject NOTIFICATION without any handler registered → no crash
    uint8_t payload[] = {0x01};
    MessageHeader hdr = make_notification(0x2000, 0x8001, 1);
    inject_and_process(hdr, payload, 1, make_addr(20), 0);
    // Just ensure no crash; no handler means silently dropped
}

// ── SD message routing ──────────────────────────────────────────

TEST_F(RuntimeTest, SdOfferMessage_ParsesToConsumer) {
    rt->find_service(0x1000, 1, 0);

    // Build an SD_OFFER message
    MessageHeader sd_hdr{};
    sd_hdr.version          = PROTOCOL_VERSION;
    sd_hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    sd_hdr.return_code      = 0;
    sd_hdr.flags            = 0;
    sd_hdr.service_id       = SD_SERVICE_ID;
    sd_hdr.method_event_id  = static_cast<uint16_t>(SdMethod::SD_OFFER_SERVICE);
    sd_hdr.client_id        = 0x0002;
    sd_hdr.sequence_counter = 0;
    sd_hdr.request_id       = 0;

    // Serialize offer payload
    uint8_t offer_payload[SD_OFFER_PAYLOAD_SIZE<SmallConfig>];
    sd_payload::serialize_offer<SmallConfig>(
        offer_payload, 0x1000, 1, 0, 30, 0x0001, make_addr(20));
    sd_hdr.payload_length = sizeof(offer_payload);

    inject_and_process(sd_hdr, offer_payload, sizeof(offer_payload), make_addr(20), 100);

    // Consumer should now be in FOUND state
    auto state = rt->service_discovery().get_consumer_state(0x1000);
    EXPECT_EQ(state, ServiceDiscovery<SmallConfig>::ConsumerState::FOUND);
}

TEST_F(RuntimeTest, SdFindMessage_ProviderResponds) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);
    rt->offer_service(0x1000, 10, 0);

    // Build an SD_FIND message
    MessageHeader sd_hdr{};
    sd_hdr.version          = PROTOCOL_VERSION;
    sd_hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    sd_hdr.return_code      = 0;
    sd_hdr.flags            = 0;
    sd_hdr.service_id       = SD_SERVICE_ID;
    sd_hdr.method_event_id  = static_cast<uint16_t>(SdMethod::SD_FIND_SERVICE);
    sd_hdr.client_id        = 0x0002;
    sd_hdr.sequence_counter = 0;
    sd_hdr.request_id       = 0;

    uint8_t find_payload[SD_FIND_PAYLOAD_SIZE];
    sd_payload::serialize_find(find_payload, 0x1000);
    sd_hdr.payload_length = SD_FIND_PAYLOAD_SIZE;

    inject_and_process(sd_hdr, find_payload, SD_FIND_PAYLOAD_SIZE, make_addr(50), 100);

    // Provider should have sent a unicast offer response
    EXPECT_GE(transport.sent.size(), 1u);
}

// ── Consumer reboot via SD_FIND resets E2E state (§4.7, §7.2) ────

// Regression: when a consumer restarts it sends SD_FIND before issuing new
// requests.  Its tx_seq_ resets to 0, but the provider still had the consumer
// tracked with a high last_seen value.  The unsigned delta can exceed
// SeqCounterAcceptWindow → every request is rejected as Stale.
// The fix: handle_sd_message calls e2e_.reset_peer(source) on SD_FIND_SERVICE
// so the subsequent requests are treated as FirstSeen and accepted.
TEST_F(RuntimeTest, SdFindMessage_ResetsConsumerE2EState) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);
    rt->offer_service(0x1000, 10, 0);

    const Addr consumer = make_addr(50);

    // Phase 1: establish last_seen=20 for this consumer address.
    {
        MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
        hdr.sequence_counter = 20;
        inject_and_process(hdr, nullptr, 0, consumer, 100);
    }
    ASSERT_EQ(svc.last_method_id, 0x0001); // accepted (FirstSeen)
    transport.clear();
    svc.last_method_id = 0;

    // Phase 2: consumer "reboots" and re-sends SD_FIND_SERVICE.
    {
        MessageHeader sd_hdr{};
        sd_hdr.version          = PROTOCOL_VERSION;
        sd_hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
        sd_hdr.return_code      = 0;
        sd_hdr.flags            = 0;
        sd_hdr.service_id       = SD_SERVICE_ID;
        sd_hdr.method_event_id  = static_cast<uint16_t>(SdMethod::SD_FIND_SERVICE);
        sd_hdr.client_id        = 0x0002;
        sd_hdr.sequence_counter = 0;
        sd_hdr.request_id       = 0;

        uint8_t find_payload[SD_FIND_PAYLOAD_SIZE];
        sd_payload::serialize_find(find_payload, 0x1000);
        sd_hdr.payload_length = SD_FIND_PAYLOAD_SIZE;

        inject_and_process(sd_hdr, find_payload, SD_FIND_PAYLOAD_SIZE, consumer, 200);
    }
    transport.clear();

    // Phase 3: consumer sends the first post-reboot request with seq=1.
    // Without the fix: delta = (1 - 20) mod 256 = 237 > SeqCounterAcceptWindow(15)
    //                  → SeqResult::Stale → request dropped.
    // With    the fix: peer state was cleared by SD_FIND → SeqResult::FirstSeen
    //                  → request accepted.
    {
        MessageHeader hdr = make_request(0x1000, 0x0001, 0, 2);
        hdr.sequence_counter = 1; // small seq after reboot
        inject_and_process(hdr, nullptr, 0, consumer, 300);
    }

    EXPECT_EQ(svc.last_method_id, 0x0001); // must have been dispatched
    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::StaleMessages), 0u);
}

// Sanity check: without an intervening SD_FIND, a stale sequence IS rejected.
// This confirms the above test exercises a real code path.
TEST_F(RuntimeTest, StaleSequence_WithoutSdFind_IsRejected) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    const Addr consumer = make_addr(51);

    // Seed last_seen=20.
    {
        MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
        hdr.sequence_counter = 20;
        inject_and_process(hdr, nullptr, 0, consumer, 100);
    }
    svc.last_method_id = 0;
    transport.clear();

    // Send seq=1 without any SD_FIND reset → must be dropped as Stale.
    {
        MessageHeader hdr = make_request(0x1000, 0x0001, 0, 2);
        hdr.sequence_counter = 1;
        inject_and_process(hdr, nullptr, 0, consumer, 200);
    }

    EXPECT_EQ(svc.last_method_id, 0u); // not dispatched
    EXPECT_EQ(rt->diagnostics().get(DiagnosticCounter::StaleMessages), 1u);
}

// ── Request timeout eviction ────────────────────────────────────

TEST_F(RuntimeTest, RequestTimeout_EvictsAndCallsCallback) {
    rt->find_service(0x1000, 1, 0);
    rt->service_discovery().handle_offer(0x1000, 1, 30, 0x0001, make_addr(20), 0);

    ReturnCode timeout_rc = ReturnCode::E_OK;
    auto cb = [](ReturnCode rc, const uint8_t*, std::size_t, void* ctx) {
        *static_cast<ReturnCode*>(ctx) = rc;
    };

    auto rid = rt->request(0x1000, 0x0001, nullptr, 0,
                           cb, &timeout_rc, 200, /*now_ms=*/100);
    ASSERT_TRUE(rid.has_value());

    // Process at time after timeout
    transport.clear(); // clear the sent request
    rt->process(600); // 600 > 100 + 200 → timed out

    EXPECT_EQ(timeout_rc, ReturnCode::E_TIMEOUT);
}

// ── Subscription eviction ───────────────────────────────────────

TEST_F(RuntimeTest, EventSubscriptionExpiry_RemovesSubscriber) {
    rt->register_event(0x1000, 0x8001);

    // Simulate an incoming subscribe from a remote client
    MessageHeader sd_hdr{};
    sd_hdr.version          = PROTOCOL_VERSION;
    sd_hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    sd_hdr.return_code      = 0;
    sd_hdr.flags            = 0;
    sd_hdr.service_id       = SD_SERVICE_ID;
    sd_hdr.method_event_id  = static_cast<uint16_t>(SdMethod::SD_SUBSCRIBE_EVENT);
    sd_hdr.client_id        = 0x0020;
    sd_hdr.sequence_counter = 0;
    sd_hdr.request_id       = 0;

    uint8_t sub_payload[SD_SUBSCRIBE_PAYLOAD_SIZE];
    sd_payload::serialize_subscribe(sub_payload, 0x1000, 0x8001, 1); // TTL=1s
    sd_hdr.payload_length = SD_SUBSCRIBE_PAYLOAD_SIZE;

    inject_and_process(sd_hdr, sub_payload, SD_SUBSCRIBE_PAYLOAD_SIZE,
                       make_addr(30), 0);

    EXPECT_TRUE(rt->event_manager().has_subscribers(0x1000, 0x8001));

    // Process after TTL expiry
    rt->process(2000); // 2s > 1s TTL

    EXPECT_FALSE(rt->event_manager().has_subscribers(0x1000, 0x8001));
}

// ── HMAC authentication round-trip ──────────────────────────────

TEST_F(RuntimeTest, Auth_ValidHmac_Accepted) {
    Addr peer = make_addr(10);
    uint8_t key[SmallConfig::HmacKeySize];
    std::memset(key, 0x42, sizeof(key));

    rt->set_hmac_key(peer, key);

    StubService svc;
    svc.response_data = {0xCC};
    rt->register_service(0x1000, svc, 1, 0);

    // Build a message with valid HMAC
    uint8_t payload[] = {0x01};
    MessageHeader hdr = make_request(0x1000, 0x0001, 1, 1);
    hdr.flags = FLAG_AUTH;

    // We need to compute the correct HMAC ourselves
    // First serialize header + payload, then compute HMAC
    uint8_t msg_buf[256];
    hdr.serialize(msg_buf);
    std::memcpy(msg_buf + HEADER_SIZE, payload, 1);

    MessageAuthenticator<SmallConfig> auth;
    auth.set_key(peer, key);
    uint8_t hmac[HMAC_SIZE]{};
    auth.compute(msg_buf, msg_buf + HEADER_SIZE, 1, peer, hmac);

    inject_and_process(hdr, payload, 1, peer, 0, hmac);

    // Should have been accepted → response sent
    ASSERT_EQ(transport.sent.size(), 1u);
    MessageHeader resp_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(resp_hdr.message_type,
              static_cast<uint8_t>(MessageType::RESPONSE));
}

// ── Process cycle: multiple messages in one call ────────────────

TEST_F(RuntimeTest, ProcessDrainsMultipleMessages) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    for (int i = 0; i < 3; ++i) {
        MessageHeader hdr = make_request(0x1000, 0x0001, 0, static_cast<uint32_t>(i + 1));
        hdr.sequence_counter = static_cast<uint8_t>(i);
        auto raw = build_raw_message(hdr);
        transport.receive_queue.push_back({make_addr(static_cast<uint8_t>(10 + i)), raw});
    }

    rt->process(0);

    // All 3 requests should have been handled, 3 responses sent
    EXPECT_EQ(transport.sent.size(), 3u);
}

// ── Diagnostic callback integration ─────────────────────────────

TEST_F(RuntimeTest, DiagnosticCallback_FiredOnError) {
    int call_count = 0;
    auto diag_cb = [](DiagnosticCounter counter, const uint8_t*, void* ctx) {
        ++(*static_cast<int*>(ctx));
        (void)counter;
    };
    rt->set_diagnostic_callback(diag_cb, &call_count);

    // Inject a too-short message → DroppedMessages
    std::vector<uint8_t> tiny = {0x01};
    transport.receive_queue.push_back({make_addr(10), tiny});
    rt->process(0);

    EXPECT_GE(call_count, 1);
}

// ── Send failure increments dropped count ───────────────────────

TEST_F(RuntimeTest, SendFailure_IncrementsDroppedMessages) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);

    transport.send_fails = true;

    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    inject_and_process(hdr, nullptr, 0, make_addr(10), 0);

    EXPECT_GE(rt->diagnostics().get(DiagnosticCounter::DroppedMessages), 1u);
}

// ── Offer/Stop from Runtime ─────────────────────────────────────

TEST_F(RuntimeTest, OfferService_RequiresRegisteredService) {
    EXPECT_FALSE(rt->offer_service(0x1000, 10, 0)); // not registered
}

TEST_F(RuntimeTest, OfferService_Success_ProcessBroadcasts) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);
    EXPECT_TRUE(rt->offer_service(0x1000, 10, 0));

    rt->process(0); // fires the broadcast
    EXPECT_GE(transport.broadcasts.size(), 1u);
}

TEST_F(RuntimeTest, StopOffer_NoMoreBroadcasts) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);
    rt->offer_service(0x1000, 10, 0);

    rt->process(0); // initial broadcast
    transport.clear();

    rt->stop_offer(0x1000);
    rt->process(5000); // long after TTL/2
    EXPECT_EQ(transport.broadcasts.size(), 0u);
}

// ── UnregisterService ───────────────────────────────────────────

TEST_F(RuntimeTest, UnregisterService_RemovesFromDispatcher) {
    StubService svc;
    rt->register_service(0x1000, svc, 1, 0);
    EXPECT_TRUE(rt->unregister_service(0x1000));

    // Now a request should get UNKNOWN_SERVICE error
    MessageHeader hdr = make_request(0x1000, 0x0001, 0, 1);
    inject_and_process(hdr, nullptr, 0, make_addr(10), 0);

    ASSERT_EQ(transport.sent.size(), 1u);
    MessageHeader resp_hdr = MessageHeader::deserialize(transport.sent[0].data.data());
    EXPECT_EQ(resp_hdr.return_code,
              static_cast<uint8_t>(ReturnCode::E_UNKNOWN_SERVICE));
}
