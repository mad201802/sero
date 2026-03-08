#pragma once
/// @file test_helpers.hpp
/// Shared test utilities: SmallConfig, MockTransport, StubService, StubEventHandler,
/// and helpers for building raw SOME/IP messages for injection.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <sero.hpp>

namespace test {

// ── Small Config for edge-case testing (small tables) ────────────

struct SmallConfig {
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
    static constexpr sero::LogLevel MinLogLevel              = sero::LogLevel::Off;
};

using Addr = sero::Address<SmallConfig>;

// ── Mock Transport ──────────────────────────────────────────────

struct SentMessage {
    Addr destination;
    std::vector<uint8_t> data;
};

struct BroadcastMessage {
    std::vector<uint8_t> data;
};

struct InjectMessage {
    Addr source;
    std::vector<uint8_t> data;
};

class MockTransport : public sero::ITransport<MockTransport, SmallConfig> {
public:
    // Storage for sent unicast messages
    std::vector<SentMessage> sent;
    // Storage for broadcast messages
    std::vector<BroadcastMessage> broadcasts;
    // Queue for injecting poll results
    std::vector<InjectMessage> receive_queue;

    // Control: force send/broadcast to fail
    bool send_fails = false;
    bool broadcast_fails = false;

    bool impl_send(const Addr& destination, const uint8_t* data, std::size_t length) {
        if (send_fails) return false;
        sent.push_back({destination, std::vector<uint8_t>(data, data + length)});
        return true;
    }

    bool impl_broadcast(const uint8_t* data, std::size_t length) {
        if (broadcast_fails) return false;
        broadcasts.push_back({std::vector<uint8_t>(data, data + length)});
        return true;
    }

    bool impl_poll(Addr& source, const uint8_t*& data, std::size_t& length) {
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
    InjectMessage current_; // keeps data alive between poll calls
};

// ── Stub Service (IService CRTP) ────────────────────────────────

class StubService : public sero::IService<StubService> {
public:
    bool ready = true;
    sero::ReturnCode return_code = sero::ReturnCode::E_OK;

    // Last received request info
    uint16_t last_method_id = 0;
    std::vector<uint8_t> last_payload;
    std::vector<uint8_t> response_data;

    sero::ReturnCode impl_on_request(uint16_t method_id,
                                           const uint8_t* payload,
                                           std::size_t payload_length,
                                           uint8_t* response,
                                           std::size_t& response_length) {
        last_method_id = method_id;
        last_payload.assign(payload, payload + payload_length);
        if (return_code == sero::ReturnCode::E_OK && !response_data.empty()) {
            std::memcpy(response, response_data.data(), response_data.size());
            response_length = response_data.size();
        } else {
            response_length = 0;
        }
        return return_code;
    }

    bool impl_is_ready() const { return ready; }
};

// ── Stub Event Handler (IEventHandler CRTP) ─────────────────────

struct ReceivedEvent {
    uint16_t service_id;
    uint16_t event_id;
    std::vector<uint8_t> payload;
};

class StubEventHandler : public sero::IEventHandler<StubEventHandler> {
public:
    std::vector<ReceivedEvent> events;

    void impl_on_event(uint16_t service_id, uint16_t event_id,
                       const uint8_t* payload, std::size_t payload_length) {
        events.push_back({service_id, event_id,
                          std::vector<uint8_t>(payload, payload + payload_length)});
    }
};

// ── Message Builder Helper ──────────────────────────────────────

/// Build a complete raw SOME/IP message (header + payload + optional HMAC + CRC)
/// suitable for injection into MockTransport::receive_queue.
inline std::vector<uint8_t> build_raw_message(
    const sero::MessageHeader& hdr,
    const uint8_t* payload = nullptr,
    std::size_t payload_length = 0,
    const uint8_t* hmac = nullptr)  // 16 bytes if present
{
    std::size_t total = sero::HEADER_SIZE + payload_length
        + (hmac ? sero::HMAC_SIZE : 0) + sero::CRC_SIZE;

    std::vector<uint8_t> buf(total, 0);
    hdr.serialize(buf.data());

    if (payload && payload_length > 0) {
        std::memcpy(buf.data() + sero::HEADER_SIZE, payload, payload_length);
    }

    std::size_t offset = sero::HEADER_SIZE + payload_length;
    if (hmac) {
        std::memcpy(buf.data() + offset, hmac, sero::HMAC_SIZE);
        offset += sero::HMAC_SIZE;
    }

    // Append CRC
    sero::crc16_append(buf.data(), offset);

    return buf;
}

/// Make a minimal valid address with a single distinguishing byte.
inline Addr make_addr(uint8_t id) {
    Addr a{};
    a[0] = id;
    return a;
}

} // namespace test
