/// @file test_logging.cpp
/// Unit tests for sero's structured logging system.

#include <gtest/gtest.h>
#include <sero.hpp>
#include "test_helpers.hpp"

#include <cstring>
#include <vector>

using namespace sero;

// ── Logging-enabled Config ──────────────────────────────────────

struct LogConfig {
    static constexpr std::size_t MaxPayloadSize           = 256;
    static constexpr std::size_t MaxServices               = 4;
    static constexpr std::size_t MaxMethods                = 8;
    static constexpr std::size_t MaxEvents                 = 4;
    static constexpr std::size_t MaxSubscribers            = 3;
    static constexpr std::size_t MaxPendingRequests        = 4;
    static constexpr std::size_t MaxKnownServices          = 4;
    static constexpr uint32_t    RequestTimeoutMs          = 500;
    static constexpr uint16_t    OfferTtlSeconds           = 3;
    static constexpr uint16_t    SubscriptionTtlSeconds    = 5;
    static constexpr uint8_t     SdFindRetryCount          = 2;
    static constexpr uint32_t    SdFindInitialDelayMs      = 50;
    static constexpr uint8_t     SdFindBackoffMultiplier   = 2;
    static constexpr uint32_t    SdFindJitterMs            = 10;
    static constexpr uint8_t     SeqCounterAcceptWindow    = 15;
    static constexpr std::size_t TransportAddressSize      = 8;
    static constexpr std::size_t MaxReceiveQueueSize       = 8;
    static constexpr std::size_t MaxTrackedPeers           = 4;
    static constexpr std::size_t HmacKeySize               = 32;
    static constexpr std::size_t MaxDtcs                    = 4;
    static constexpr LogLevel    MinLogLevel                = LogLevel::Trace;
};

using LogAddr = Address<LogConfig>;

// ── Mock Transport for LogConfig ───────────────────────────────

struct LogSentMessage {
    LogAddr destination;
    std::vector<uint8_t> data;
};

struct LogInjectMessage {
    LogAddr source;
    std::vector<uint8_t> data;
};

class LogMockTransport : public ITransport<LogMockTransport, LogConfig> {
public:
    std::vector<LogSentMessage> sent;
    std::vector<std::vector<uint8_t>> broadcasts;
    std::vector<LogInjectMessage> receive_queue;

    bool send_fails = false;
    bool broadcast_fails = false;

    bool impl_send(const LogAddr& destination, const uint8_t* data, std::size_t length) {
        if (send_fails) return false;
        sent.push_back({destination, std::vector<uint8_t>(data, data + length)});
        return true;
    }

    bool impl_broadcast(const uint8_t* data, std::size_t length) {
        if (broadcast_fails) return false;
        broadcasts.push_back(std::vector<uint8_t>(data, data + length));
        return true;
    }

    bool impl_poll(LogAddr& source, const uint8_t*& data, std::size_t& length) {
        if (receive_queue.empty()) return false;
        current_ = std::move(receive_queue.front());
        receive_queue.erase(receive_queue.begin());
        source = current_.source;
        data = current_.data.data();
        length = current_.data.size();
        return true;
    }

    void clear() {
        sent.clear();
        broadcasts.clear();
        receive_queue.clear();
    }

private:
    LogInjectMessage current_;
};

// ── Stub service for LogConfig ──────────────────────────────────

class LogStubService : public IService<LogStubService> {
public:
    bool ready = true;
    ReturnCode return_code = ReturnCode::E_OK;
    std::vector<uint8_t> response_data;

    ReturnCode impl_on_request(uint16_t /*method_id*/,
                               const uint8_t* /*payload*/,
                               std::size_t /*payload_length*/,
                               uint8_t* response,
                               std::size_t& response_length) {
        if (return_code == ReturnCode::E_OK && !response_data.empty()) {
            std::memcpy(response, response_data.data(), response_data.size());
            response_length = response_data.size();
        } else {
            response_length = 0;
        }
        return return_code;
    }

    bool impl_is_ready() const { return ready; }
};

// ── Test-local log record collector ─────────────────────────────

struct LogRecord {
    LogLevel    level;
    LogCategory category;
    const char* tag;
    uint16_t    service_id;
    uint16_t    method_event_id;
    uint16_t    client_id;
    uint32_t    extra;
};

static std::vector<LogRecord> g_log_records;

static void test_log_callback(const LogEntry& entry, void* /*ctx*/) {
    g_log_records.push_back({entry.level, entry.category, entry.tag,
                             entry.service_id, entry.method_event_id,
                             entry.client_id, entry.extra});
}

// Helper to build a raw message with LogConfig
static std::vector<uint8_t> build_log_raw_message(
    const MessageHeader& hdr,
    const uint8_t* payload = nullptr,
    std::size_t payload_length = 0)
{
    std::size_t total = HEADER_SIZE + payload_length + CRC_SIZE;
    std::vector<uint8_t> buf(total, 0);
    hdr.serialize(buf.data());
    if (payload && payload_length > 0) {
        std::memcpy(buf.data() + HEADER_SIZE, payload, payload_length);
    }
    crc16_append(buf.data(), HEADER_SIZE + payload_length);
    return buf;
}

static LogAddr make_log_addr(uint8_t id) {
    LogAddr a{};
    a[0] = id;
    return a;
}

// ── Logger unit tests ───────────────────────────────────────────

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override { g_log_records.clear(); }
    void TearDown() override { g_log_records.clear(); }
};

TEST_F(LoggerTest, CallbackReceivesCorrectEntry) {
    Logger<LogConfig> logger;
    logger.set_callback(test_log_callback, nullptr);

    logger.warn(LogCategory::Validation, "crc_fail", 0x1234, 0x0001, 0x0042, 100);

    ASSERT_EQ(g_log_records.size(), 1u);
    EXPECT_EQ(g_log_records[0].level, LogLevel::Warn);
    EXPECT_EQ(g_log_records[0].category, LogCategory::Validation);
    EXPECT_STREQ(g_log_records[0].tag, "crc_fail");
    EXPECT_EQ(g_log_records[0].service_id, 0x1234);
    EXPECT_EQ(g_log_records[0].method_event_id, 0x0001);
    EXPECT_EQ(g_log_records[0].client_id, 0x0042);
    EXPECT_EQ(g_log_records[0].extra, 100u);
}

TEST_F(LoggerTest, NoCallbackDoesNotCrash) {
    Logger<LogConfig> logger;
    // No callback set — should be safe
    logger.error(LogCategory::Auth, "test", 0, 0, 0, 0);
    EXPECT_EQ(g_log_records.size(), 0u);
}

TEST_F(LoggerTest, AllLevelsReachCallback) {
    Logger<LogConfig> logger;
    logger.set_callback(test_log_callback, nullptr);

    logger.trace(LogCategory::General, "t");
    logger.debug(LogCategory::General, "d");
    logger.info(LogCategory::General, "i");
    logger.warn(LogCategory::General, "w");
    logger.error(LogCategory::General, "e");

    EXPECT_EQ(g_log_records.size(), 5u);
    EXPECT_EQ(g_log_records[0].level, LogLevel::Trace);
    EXPECT_EQ(g_log_records[1].level, LogLevel::Debug);
    EXPECT_EQ(g_log_records[2].level, LogLevel::Info);
    EXPECT_EQ(g_log_records[3].level, LogLevel::Warn);
    EXPECT_EQ(g_log_records[4].level, LogLevel::Error);
}

// ── Compile-time level filtering test (Off config) ──────────────

struct OffConfig {
    static constexpr std::size_t MaxPayloadSize           = 256;
    static constexpr std::size_t MaxServices               = 4;
    static constexpr std::size_t MaxMethods                = 8;
    static constexpr std::size_t MaxEvents                 = 4;
    static constexpr std::size_t MaxSubscribers            = 3;
    static constexpr std::size_t MaxPendingRequests        = 4;
    static constexpr std::size_t MaxKnownServices          = 4;
    static constexpr uint32_t    RequestTimeoutMs          = 500;
    static constexpr uint16_t    OfferTtlSeconds           = 3;
    static constexpr uint16_t    SubscriptionTtlSeconds    = 5;
    static constexpr uint8_t     SdFindRetryCount          = 2;
    static constexpr uint32_t    SdFindInitialDelayMs      = 50;
    static constexpr uint8_t     SdFindBackoffMultiplier   = 2;
    static constexpr uint32_t    SdFindJitterMs            = 10;
    static constexpr uint8_t     SeqCounterAcceptWindow    = 15;
    static constexpr std::size_t TransportAddressSize      = 8;
    static constexpr std::size_t MaxReceiveQueueSize       = 8;
    static constexpr std::size_t MaxTrackedPeers           = 4;
    static constexpr std::size_t HmacKeySize               = 32;
    static constexpr std::size_t MaxDtcs                    = 4;
    static constexpr LogLevel    MinLogLevel                = LogLevel::Off;
};

TEST_F(LoggerTest, OffConfigNeverCallsCallback) {
    Logger<OffConfig> logger;
    logger.set_callback(test_log_callback, nullptr);

    logger.trace(LogCategory::General, "t");
    logger.debug(LogCategory::General, "d");
    logger.info(LogCategory::General, "i");
    logger.warn(LogCategory::General, "w");
    logger.error(LogCategory::General, "e");

    EXPECT_EQ(g_log_records.size(), 0u);
}

// ── Warn-only config ────────────────────────────────────────────

struct WarnConfig {
    static constexpr std::size_t MaxPayloadSize           = 256;
    static constexpr std::size_t MaxServices               = 4;
    static constexpr std::size_t MaxMethods                = 8;
    static constexpr std::size_t MaxEvents                 = 4;
    static constexpr std::size_t MaxSubscribers            = 3;
    static constexpr std::size_t MaxPendingRequests        = 4;
    static constexpr std::size_t MaxKnownServices          = 4;
    static constexpr uint32_t    RequestTimeoutMs          = 500;
    static constexpr uint16_t    OfferTtlSeconds           = 3;
    static constexpr uint16_t    SubscriptionTtlSeconds    = 5;
    static constexpr uint8_t     SdFindRetryCount          = 2;
    static constexpr uint32_t    SdFindInitialDelayMs      = 50;
    static constexpr uint8_t     SdFindBackoffMultiplier   = 2;
    static constexpr uint32_t    SdFindJitterMs            = 10;
    static constexpr uint8_t     SeqCounterAcceptWindow    = 15;
    static constexpr std::size_t TransportAddressSize      = 8;
    static constexpr std::size_t MaxReceiveQueueSize       = 8;
    static constexpr std::size_t MaxTrackedPeers           = 4;
    static constexpr std::size_t HmacKeySize               = 32;
    static constexpr std::size_t MaxDtcs                    = 4;
    static constexpr LogLevel    MinLogLevel                = LogLevel::Warn;
};

TEST_F(LoggerTest, WarnConfigFiltersLowerLevels) {
    Logger<WarnConfig> logger;
    logger.set_callback(test_log_callback, nullptr);

    logger.trace(LogCategory::General, "t");
    logger.debug(LogCategory::General, "d");
    logger.info(LogCategory::General, "i");
    logger.warn(LogCategory::General, "w");
    logger.error(LogCategory::General, "e");

    ASSERT_EQ(g_log_records.size(), 2u);
    EXPECT_EQ(g_log_records[0].level, LogLevel::Warn);
    EXPECT_EQ(g_log_records[1].level, LogLevel::Error);
}

// ── Runtime integration tests ───────────────────────────────────

class RuntimeLoggingTest : public ::testing::Test {
protected:
    LogMockTransport transport;
    Runtime<LogMockTransport, LogConfig> runtime{transport, 0x0042};

    void SetUp() override {
        g_log_records.clear();
        runtime.set_log_callback(test_log_callback, nullptr);
    }
    void TearDown() override { g_log_records.clear(); }
};

TEST_F(RuntimeLoggingTest, CrcFailureLogsError) {
    // Inject a message with bad CRC
    LogAddr source = make_log_addr(1);
    std::vector<uint8_t> bad_msg(22, 0); // MIN_MESSAGE_SIZE but bad CRC
    transport.receive_queue.push_back({source, bad_msg});

    runtime.process(1000);

    bool found = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::Validation &&
            std::string(rec.tag) == "crc_fail") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Error);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected crc_fail log entry not found";
}

TEST_F(RuntimeLoggingTest, MinSizeFailLogsWarn) {
    LogAddr source = make_log_addr(1);
    std::vector<uint8_t> tiny_msg(5, 0); // Too small
    transport.receive_queue.push_back({source, tiny_msg});

    runtime.process(1000);

    bool found = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::Validation &&
            std::string(rec.tag) == "min_size_fail") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Warn);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected min_size_fail log entry not found";
}

TEST_F(RuntimeLoggingTest, SuccessfulDispatchLogsDebug) {
    LogStubService svc;
    ASSERT_TRUE(runtime.register_service(0x1000, svc, 1, 0));

    // Build a valid REQUEST message
    MessageHeader hdr{};
    hdr.version         = PROTOCOL_VERSION;
    hdr.message_type    = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.return_code     = 0;
    hdr.flags           = 0;
    hdr.service_id      = 0x1000;
    hdr.method_event_id = 0x0001;
    hdr.client_id       = 0x0042;
    hdr.sequence_counter = 1;
    hdr.request_id      = 1;
    hdr.payload_length  = 0;

    auto raw = build_log_raw_message(hdr);
    LogAddr source = make_log_addr(1);
    transport.receive_queue.push_back({source, raw});

    runtime.process(1000);

    bool found_dispatch = false;
    bool found_request = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::Dispatch &&
            std::string(rec.tag) == "dispatch") {
            found_dispatch = true;
            EXPECT_EQ(rec.level, LogLevel::Debug);
            EXPECT_EQ(rec.service_id, 0x1000);
        }
        if (rec.category == LogCategory::Dispatch &&
            std::string(rec.tag) == "request_rx") {
            found_request = true;
            EXPECT_EQ(rec.level, LogLevel::Info);
        }
    }
    EXPECT_TRUE(found_dispatch) << "Expected dispatch log entry not found";
    EXPECT_TRUE(found_request) << "Expected request_rx log entry not found";
}

TEST_F(RuntimeLoggingTest, VersionMismatchLogsWarn) {
    MessageHeader hdr{};
    hdr.version         = 0xFF; // wrong version
    hdr.message_type    = static_cast<uint8_t>(MessageType::REQUEST);
    hdr.return_code     = 0;
    hdr.flags           = 0;
    hdr.service_id      = 0x1000;
    hdr.method_event_id = 0x0001;
    hdr.client_id       = 0x0042;
    hdr.sequence_counter = 1;
    hdr.request_id      = 1;
    hdr.payload_length  = 0;

    auto raw = build_log_raw_message(hdr);
    LogAddr source = make_log_addr(1);
    transport.receive_queue.push_back({source, raw});

    runtime.process(1000);

    bool found = false;
    for (auto& rec : g_log_records) {
        if (std::string(rec.tag) == "version_mismatch") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Warn);
            EXPECT_EQ(rec.extra, 0xFFu);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected version_mismatch log entry not found";
}

TEST_F(RuntimeLoggingTest, ServiceRegistrationLogsInfo) {
    LogStubService svc;
    runtime.register_service(0x2000, svc, 1, 0);

    bool found = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::Methods &&
            std::string(rec.tag) == "svc_registered") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Info);
            EXPECT_EQ(rec.service_id, 0x2000);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected svc_registered log entry not found";
}

TEST_F(RuntimeLoggingTest, SdOfferLogsInfo) {
    LogStubService svc;
    ASSERT_TRUE(runtime.register_service(0x3000, svc, 1, 0));
    ASSERT_TRUE(runtime.offer_service(0x3000, 5, 1000));

    bool found = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::ServiceDiscovery &&
            std::string(rec.tag) == "offer_start") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Info);
            EXPECT_EQ(rec.service_id, 0x3000);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected offer_start log entry not found";
}

TEST_F(RuntimeLoggingTest, RequestTimeoutLogsWarn) {
    LogStubService svc;
    ASSERT_TRUE(runtime.register_service(0x4000, svc, 1, 0));
    ASSERT_TRUE(runtime.offer_service(0x4000, 5, 1000));

    // Find the service from a "consumer" perspective by simulating discovery
    ASSERT_TRUE(runtime.find_service(0x4000, 1, 1000));

    // Simulate offer received (so provider address is known)
    MessageHeader offer_hdr{};
    offer_hdr.version         = PROTOCOL_VERSION;
    offer_hdr.message_type    = static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN);
    offer_hdr.return_code     = 0;
    offer_hdr.flags           = 0;
    offer_hdr.service_id      = SD_SERVICE_ID;
    offer_hdr.method_event_id = static_cast<uint16_t>(SdMethod::SD_OFFER_SERVICE);
    offer_hdr.client_id       = 0x0001;
    offer_hdr.sequence_counter = 0;
    offer_hdr.request_id      = 0;

    constexpr std::size_t OFFER_PAYLOAD_SIZE = 8 + LogConfig::TransportAddressSize;
    offer_hdr.payload_length = OFFER_PAYLOAD_SIZE;

    uint8_t offer_payload[OFFER_PAYLOAD_SIZE]{};
    LogAddr provider_addr = make_log_addr(99);
    MessageHeader::write_u16(offer_payload, 0x4000);     // service_id
    offer_payload[2] = 1;                                  // major
    offer_payload[3] = 0;                                  // minor
    MessageHeader::write_u16(offer_payload + 4, 5);       // TTL
    MessageHeader::write_u16(offer_payload + 6, 0x1234);  // session_id
    std::memcpy(offer_payload + 8, provider_addr.data(), LogConfig::TransportAddressSize);

    auto offer_raw = build_log_raw_message(offer_hdr, offer_payload, OFFER_PAYLOAD_SIZE);
    transport.receive_queue.push_back({provider_addr, offer_raw});
    runtime.process(1000);

    g_log_records.clear();

    // Send a request that will timeout
    auto cb = [](ReturnCode, const uint8_t*, std::size_t, void*) {};
    auto rid = runtime.request(0x4000, 0x0001, nullptr, 0, cb, nullptr, 100, 2000);

    // Advance time past deadline
    runtime.process(3000);

    bool found = false;
    for (auto& rec : g_log_records) {
        if (rec.category == LogCategory::Requests &&
            std::string(rec.tag) == "req_timeout") {
            found = true;
            EXPECT_EQ(rec.level, LogLevel::Warn);
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected req_timeout log entry not found";
}

// ── Default config test (MinLogLevel::Off) ──────────────────────

TEST(DefaultConfigLogging, NoLogsWithDefaultConfig) {
    // SmallConfig from test_helpers doesn't define MinLogLevel.
    // DefaultConfig has MinLogLevel::Off — confirm it compiles and
    // produces no log output.
    test::MockTransport transport;
    Runtime<test::MockTransport, DefaultConfig> runtime{transport, 0x0042};

    // Even if we set a callback, Off level should mean zero calls
    runtime.set_log_callback(test_log_callback, nullptr);
    g_log_records.clear();

    runtime.process(1000);

    // No messages to process, but also no logs should fire
    EXPECT_EQ(g_log_records.size(), 0u);
}
