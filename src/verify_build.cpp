/// @file verify_build.cpp
/// Minimal compilation test — instantiates core types to verify the library compiles.

#include <sero.hpp>
#include <cstdio>

using namespace sero;

// ── Mock transport ──────────────────────────────────────────────────

class MockTransport : public ITransport<MockTransport, DefaultConfig> {
public:
    using Addr = Address<DefaultConfig>;

    bool impl_send(const Addr& /*dest*/, const uint8_t* /*data*/, std::size_t /*len*/) {
        return true;
    }
    bool impl_broadcast(const uint8_t* /*data*/, std::size_t /*len*/) {
        return true;
    }
    bool impl_poll(Addr& /*src*/, const uint8_t*& /*data*/, std::size_t& /*len*/) {
        return false; // queue empty
    }
};

// ── Mock service ────────────────────────────────────────────────────

class EchoService : public IService<EchoService> {
public:
    ReturnCode impl_on_request(uint16_t /*method_id*/,
                               const uint8_t* payload, std::size_t payload_length,
                               uint8_t* response, std::size_t& response_length) {
        if (response && payload_length <= DefaultConfig::MaxPayloadSize) {
            std::memcpy(response, payload, payload_length);
            response_length = payload_length;
        }
        return ReturnCode::E_OK;
    }
    bool impl_is_ready() const { return true; }
};

// ── Mock event handler ──────────────────────────────────────────────

class MyEventHandler : public IEventHandler<MyEventHandler> {
public:
    void impl_on_event(uint16_t /*service_id*/, uint16_t /*event_id*/,
                       const uint8_t* /*payload*/, std::size_t /*payload_length*/) {
        // no-op
    }
};

int main() {
    MockTransport transport;
    Runtime<MockTransport, DefaultConfig> rt(transport, 0x0001);

    Address<DefaultConfig> local_addr{};
    local_addr[0] = 0x01;
    rt.set_local_address(local_addr);

    EchoService echo;
    rt.register_service(0x0100, echo, 1, 0);
    rt.register_event(0x0100, 0x8001);
    rt.offer_service(0x0100, 5, 0);

    // Run one process cycle
    rt.process(1000);

    std::printf("Sero build verification passed.\n");
    return 0;
}
