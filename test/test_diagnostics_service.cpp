/// @file test_diagnostics_service.cpp
/// Unit tests for the built-in DiagnosticsService (§10).

#include <gtest/gtest.h>
#include "test_helpers.hpp"

using namespace sero;

// ── Helper: set up a DiagnosticsService with wired-up internals ─

struct DiagFixture {
    DtcStore<test::SmallConfig>           dtcs;
    DiagnosticCounters                    counters;
    MethodDispatcher<test::SmallConfig>   dispatcher;
    uint16_t                              client_id  = 0x0042;
    uint32_t                              uptime_ms  = 12345;
    DiagnosticsService<test::SmallConfig> service;

    uint8_t response[test::SmallConfig::MaxPayloadSize]{};
    std::size_t response_length = test::SmallConfig::MaxPayloadSize;

    DiagFixture() {
        service.init(&dtcs, &counters, &dispatcher, client_id, &uptime_ms);
    }

    ReturnCode call(DiagMethod method,
                    const uint8_t* payload = nullptr,
                    std::size_t payload_len = 0) {
        response_length = test::SmallConfig::MaxPayloadSize;
        return service.impl_on_request(
            static_cast<uint16_t>(method),
            payload, payload_len,
            response, response_length);
    }
};

// ── GET_DTCS: empty store → count=0 ────────────────────────────

TEST(DiagnosticsService, GetDtcsEmpty) {
    DiagFixture f;
    auto rc = f.call(DiagMethod::DIAG_GET_DTCS);

    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_GE(f.response_length, 2u);
    uint16_t count = MessageHeader::read_u16(f.response);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(f.response_length, 2u);
}

// ── GET_DTCS: with DTCs ─────────────────────────────────────────

TEST(DiagnosticsService, GetDtcsWithData) {
    DiagFixture f;
    f.dtcs.report(0x0010, DtcSeverity::Error, 1000);
    f.dtcs.report(0x0020, DtcSeverity::Warning, 2000);

    auto rc = f.call(DiagMethod::DIAG_GET_DTCS);
    EXPECT_EQ(rc, ReturnCode::E_OK);

    uint16_t count = MessageHeader::read_u16(f.response);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(f.response_length, 2u + 2 * 16);  // 2 + 32
}

// ── GET_DTCS: verify serialized fields ──────────────────────────

TEST(DiagnosticsService, GetDtcsSerializedFields) {
    DiagFixture f;
    f.dtcs.report(0x00AB, DtcSeverity::Fatal, 5000);
    f.dtcs.report(0x00AB, DtcSeverity::Fatal, 6000);  // re-report

    auto rc = f.call(DiagMethod::DIAG_GET_DTCS);
    EXPECT_EQ(rc, ReturnCode::E_OK);

    uint16_t count = MessageHeader::read_u16(f.response);
    EXPECT_EQ(count, 1u);

    const uint8_t* entry = f.response + 2;
    EXPECT_EQ(MessageHeader::read_u16(entry), 0x00AB);   // code
    EXPECT_EQ(entry[2], static_cast<uint8_t>(DtcSeverity::Fatal)); // severity
    EXPECT_EQ(entry[3], 1u);                               // status (active)
    EXPECT_EQ(MessageHeader::read_u32(entry + 4), 2u);    // occurrence_count
    EXPECT_EQ(MessageHeader::read_u32(entry + 8), 5000u); // first_seen
    EXPECT_EQ(MessageHeader::read_u32(entry + 12), 6000u); // last_seen
}

// ── CLEAR_DTCS: clear specific code ─────────────────────────────

TEST(DiagnosticsService, ClearSingleDtc) {
    DiagFixture f;
    f.dtcs.report(0x0001, DtcSeverity::Info, 100);
    f.dtcs.report(0x0002, DtcSeverity::Error, 200);

    uint8_t payload[2];
    MessageHeader::write_u16(payload, 0x0001);
    auto rc = f.call(DiagMethod::DIAG_CLEAR_DTCS, payload, 2);
    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(f.response_length, 0u);

    EXPECT_EQ(f.dtcs.count(), 1u);
    EXPECT_EQ(f.dtcs.find(0x0001), nullptr);
    EXPECT_NE(f.dtcs.find(0x0002), nullptr);
}

// ── CLEAR_DTCS: clear all (0xFFFF) ──────────────────────────────

TEST(DiagnosticsService, ClearAllDtcs) {
    DiagFixture f;
    f.dtcs.report(0x0001, DtcSeverity::Info, 100);
    f.dtcs.report(0x0002, DtcSeverity::Error, 200);

    uint8_t payload[2];
    MessageHeader::write_u16(payload, 0xFFFF);
    auto rc = f.call(DiagMethod::DIAG_CLEAR_DTCS, payload, 2);
    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(f.dtcs.count(), 0u);
}

// ── CLEAR_DTCS: malformed (too short) ───────────────────────────

TEST(DiagnosticsService, ClearDtcsMalformed) {
    DiagFixture f;
    uint8_t payload[1] = {0x00};
    auto rc = f.call(DiagMethod::DIAG_CLEAR_DTCS, payload, 1);
    EXPECT_EQ(rc, ReturnCode::E_MALFORMED_MESSAGE);
}

// ── GET_COUNTERS ────────────────────────────────────────────────

TEST(DiagnosticsService, GetCounters) {
    DiagFixture f;
    f.counters.increment(DiagnosticCounter::CrcErrors);
    f.counters.increment(DiagnosticCounter::CrcErrors);
    f.counters.increment(DiagnosticCounter::AuthFailures);

    auto rc = f.call(DiagMethod::DIAG_GET_COUNTERS);
    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(f.response_length, 36u);

    EXPECT_EQ(MessageHeader::read_u32(f.response + 0 * 4), 2u);  // CrcErrors
    EXPECT_EQ(MessageHeader::read_u32(f.response + 6 * 4), 1u);  // AuthFailures

    // Others should be zero
    EXPECT_EQ(MessageHeader::read_u32(f.response + 1 * 4), 0u);
    EXPECT_EQ(MessageHeader::read_u32(f.response + 8 * 4), 0u);
}

// ── GET_SERVICE_LIST ────────────────────────────────────────────

TEST(DiagnosticsService, GetServiceList) {
    DiagFixture f;

    // Register some stub services in the dispatcher
    test::StubService svc1;
    test::StubService svc2;
    svc2.ready = false;

    f.dispatcher.register_service(
        make_service_entry(0x1000, svc1, 1, 2, false));
    f.dispatcher.register_service(
        make_service_entry(0x2000, svc2, 3, 0, true));

    auto rc = f.call(DiagMethod::DIAG_GET_SERVICE_LIST);
    EXPECT_EQ(rc, ReturnCode::E_OK);

    uint16_t count = MessageHeader::read_u16(f.response);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(f.response_length, 2u + 2 * 6);  // 14

    // First service
    const uint8_t* e1 = f.response + 2;
    EXPECT_EQ(MessageHeader::read_u16(e1), 0x1000);
    EXPECT_EQ(e1[2], 1u);  // major
    EXPECT_EQ(e1[3], 2u);  // minor
    EXPECT_EQ(e1[4], 0u);  // auth_required = false
    EXPECT_EQ(e1[5], 1u);  // ready = true

    // Second service
    const uint8_t* e2 = f.response + 2 + 6;
    EXPECT_EQ(MessageHeader::read_u16(e2), 0x2000);
    EXPECT_EQ(e2[2], 3u);  // major
    EXPECT_EQ(e2[3], 0u);  // minor
    EXPECT_EQ(e2[4], 1u);  // auth_required = true
    EXPECT_EQ(e2[5], 0u);  // ready = false
}

// ── GET_DEVICE_INFO ─────────────────────────────────────────────

TEST(DiagnosticsService, GetDeviceInfo) {
    DiagFixture f;
    f.uptime_ms = 999000;

    auto rc = f.call(DiagMethod::DIAG_GET_DEVICE_INFO);
    EXPECT_EQ(rc, ReturnCode::E_OK);
    EXPECT_EQ(f.response_length, 8u);

    EXPECT_EQ(MessageHeader::read_u16(f.response), 0x0042);      // client_id
    EXPECT_EQ(MessageHeader::read_u32(f.response + 2), 999000u); // uptime_ms
    EXPECT_EQ(f.response[6], PROTOCOL_VERSION);                   // version
    EXPECT_EQ(f.response[7], 0u);                                 // reserved
}

// ── Unknown method → E_UNKNOWN_METHOD ───────────────────────────

TEST(DiagnosticsService, UnknownMethod) {
    DiagFixture f;
    auto rc = f.call(static_cast<DiagMethod>(0x00FF));
    EXPECT_EQ(rc, ReturnCode::E_UNKNOWN_METHOD);
    EXPECT_EQ(f.response_length, 0u);
}

// ── is_ready returns false before init ──────────────────────────

TEST(DiagnosticsService, NotReadyBeforeInit) {
    DiagnosticsService<test::SmallConfig> svc;
    EXPECT_FALSE(svc.impl_is_ready());
}

// ── is_ready returns true after init ────────────────────────────

TEST(DiagnosticsService, ReadyAfterInit) {
    DiagFixture f;
    EXPECT_TRUE(f.service.impl_is_ready());
}

// ── Runtime integration: enable_diagnostics ─────────────────────

TEST(DiagnosticsService_Runtime, EnableDiagnostics) {
    test::MockTransport transport;
    Runtime<test::MockTransport, test::SmallConfig> rt(transport, 0x0042);
    test::Addr local = test::make_addr(1);
    rt.set_local_address(local);

    uint32_t now = 1000;
    EXPECT_TRUE(rt.enable_diagnostics(now));

    // Should be idempotent
    EXPECT_TRUE(rt.enable_diagnostics(now));

    // Process to emit the SD offer
    rt.process(now);

    // The SD offer should have been broadcast
    EXPECT_GE(transport.broadcasts.size(), 1u);
}

// ── Runtime integration: report_dtc + dtc_store accessor ────────

TEST(DiagnosticsService_Runtime, ReportDtc) {
    test::MockTransport transport;
    Runtime<test::MockTransport, test::SmallConfig> rt(transport, 0x0042);
    test::Addr local = test::make_addr(1);
    rt.set_local_address(local);

    EXPECT_TRUE(rt.report_dtc(0x0010, DtcSeverity::Error, 5000));
    EXPECT_EQ(rt.dtc_store().count(), 1u);

    const Dtc* d = rt.dtc_store().find(0x0010);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->severity, static_cast<uint8_t>(DtcSeverity::Error));
}

// ── Runtime integration: clear DTCs ─────────────────────────────

TEST(DiagnosticsService_Runtime, ClearDtcs) {
    test::MockTransport transport;
    Runtime<test::MockTransport, test::SmallConfig> rt(transport, 0x0042);

    (void)rt.report_dtc(0x0001, DtcSeverity::Info, 100);
    (void)rt.report_dtc(0x0002, DtcSeverity::Warning, 200);

    EXPECT_TRUE(rt.clear_dtc(0x0001));
    EXPECT_EQ(rt.dtc_store().count(), 1u);

    rt.clear_all_dtcs();
    EXPECT_EQ(rt.dtc_store().count(), 0u);
}

// ── Runtime integration: end-to-end request/response ────────────

TEST(DiagnosticsService_Runtime, EndToEndGetDeviceInfo) {
    test::MockTransport transport;
    Runtime<test::MockTransport, test::SmallConfig> rt(transport, 0x0042);
    test::Addr local = test::make_addr(1);
    rt.set_local_address(local);

    uint32_t now = 5000;
    (void)rt.enable_diagnostics(now);
    rt.process(now);  // establish uptime

    // Build a request for DIAG_GET_DEVICE_INFO from a remote peer
    test::Addr remote = test::make_addr(2);
    MessageHeader req_hdr{};
    req_hdr.version          = PROTOCOL_VERSION;
    req_hdr.message_type     = static_cast<uint8_t>(MessageType::REQUEST);
    req_hdr.return_code      = 0;
    req_hdr.flags            = 0;
    req_hdr.service_id       = DIAG_SERVICE_ID;
    req_hdr.method_event_id  = static_cast<uint16_t>(DiagMethod::DIAG_GET_DEVICE_INFO);
    req_hdr.client_id        = 0x0099;
    req_hdr.sequence_counter = 1;
    req_hdr.request_id       = 42;
    req_hdr.payload_length   = 0;

    auto msg = test::build_raw_message(req_hdr);
    transport.receive_queue.push_back({remote, msg});

    now = 6000;
    rt.process(now);

    // Should have sent a response
    ASSERT_GE(transport.sent.size(), 1u);

    // Verify the response
    const auto& resp_data = transport.sent.back().data;
    ASSERT_GE(resp_data.size(), MIN_MESSAGE_SIZE);

    MessageHeader resp_hdr = MessageHeader::deserialize(resp_data.data());
    EXPECT_EQ(resp_hdr.service_id, DIAG_SERVICE_ID);
    EXPECT_EQ(resp_hdr.message_type, static_cast<uint8_t>(MessageType::RESPONSE));
    EXPECT_EQ(resp_hdr.return_code, static_cast<uint8_t>(ReturnCode::E_OK));
    EXPECT_EQ(resp_hdr.payload_length, 8u);

    // Parse device info payload
    const uint8_t* payload = resp_data.data() + MessageHeader::SIZE;
    EXPECT_EQ(MessageHeader::read_u16(payload), 0x0042);  // client_id
    EXPECT_EQ(payload[6], PROTOCOL_VERSION);
}
