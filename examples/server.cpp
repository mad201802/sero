/// @file server.cpp
/// Example Sero server – Calculator service with Add, Multiply, Counter event.

#include "udp_transport.hpp"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

// ── Configuration ───────────────────────────────────────────────

using Config    = sero::DefaultConfig;
using Runtime   = sero::Runtime<example::UdpTransport, Config>;
using Addr      = sero::Address<Config>;

static constexpr uint16_t SERVICE_ID  = 0x0100;
static constexpr uint16_t METHOD_ADD  = 0x0001;
static constexpr uint16_t METHOD_MUL  = 0x0002;
static constexpr uint16_t EVENT_CTR   = 0x8001;

static constexpr uint16_t SERVER_PORT = 9000;
static constexpr uint16_t MY_CLIENT_ID = 0x0001;

// ── Calculator service ──────────────────────────────────────────

class Calculator : public sero::IService<Calculator> {
public:
    bool impl_is_ready() const { return true; }

    sero::ReturnCode impl_on_request(
        uint16_t       method_id,
        const uint8_t* payload, std::size_t payload_length,
        uint8_t*       response, std::size_t& response_length)
    {
        if (method_id == METHOD_ADD) return do_add(payload, payload_length, response, response_length);
        if (method_id == METHOD_MUL) return do_mul(payload, payload_length, response, response_length);
        response_length = 0;
        return sero::ReturnCode::E_UNKNOWN_METHOD;
    }

private:
    static sero::ReturnCode do_add(const uint8_t* in, std::size_t in_len,
                                         uint8_t* out, std::size_t& out_len) {
        if (in_len < 8) { out_len = 0; return sero::ReturnCode::E_MALFORMED_MESSAGE; }
        int32_t a = read_i32(in);
        int32_t b = read_i32(in + 4);
        int32_t r = a + b;
        write_i32(out, r);
        out_len = 4;
        std::printf("[server] Add(%d, %d) = %d\n", a, b, r);
        return sero::ReturnCode::E_OK;
    }

    static sero::ReturnCode do_mul(const uint8_t* in, std::size_t in_len,
                                         uint8_t* out, std::size_t& out_len) {
        if (in_len < 8) { out_len = 0; return sero::ReturnCode::E_MALFORMED_MESSAGE; }
        int32_t a = read_i32(in);
        int32_t b = read_i32(in + 4);
        int32_t r = a * b;
        write_i32(out, r);
        out_len = 4;
        std::printf("[server] Mul(%d, %d) = %d\n", a, b, r);
        return sero::ReturnCode::E_OK;
    }

    static int32_t read_i32(const uint8_t* p) {
        return static_cast<int32_t>(
            (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) <<  8) |  uint32_t(p[3]));
    }

    static void write_i32(uint8_t* p, int32_t v) {
        auto u = static_cast<uint32_t>(v);
        p[0] = static_cast<uint8_t>(u >> 24);
        p[1] = static_cast<uint8_t>(u >> 16);
        p[2] = static_cast<uint8_t>(u >>  8);
        p[3] = static_cast<uint8_t>(u);
    }
};

// ── main ────────────────────────────────────────────────────────

int main() {
    example::UdpTransport transport;
    if (!transport.init("192.168.1.132", SERVER_PORT)) return 1;

    Runtime rt(transport, MY_CLIENT_ID);
    rt.set_local_address(transport.local_addr());

    Calculator calc;
    rt.register_service(SERVICE_ID, calc, 1, 0);

    // Register Counter event
    rt.register_event(SERVICE_ID, EVENT_CTR);

    // Offer the service via SD
    uint32_t now = example::now_ms();
    rt.offer_service(SERVICE_ID, /*ttl_seconds=*/30, now);

    std::printf("[server] Calculator service 0x%04X offered on 127.0.0.1:%u\n",
                SERVICE_ID, SERVER_PORT);

    uint32_t counter = 0;
    uint32_t last_event_ms = now;

    // ── Run loop ────────────────────────────────────────────────
    for (;;) {
        now = example::now_ms();
        rt.process(now);

        // Push Counter event every 2 seconds
        if (now - last_event_ms >= 250) {
            last_event_ms = now;
            ++counter;

            uint8_t payload[4];
            payload[0] = static_cast<uint8_t>(counter >> 24);
            payload[1] = static_cast<uint8_t>(counter >> 16);
            payload[2] = static_cast<uint8_t>(counter >>  8);
            payload[3] = static_cast<uint8_t>(counter);

            if (rt.notify_event(SERVICE_ID, EVENT_CTR, payload, 4, now)) {
                std::printf("[server] Counter event #%u sent\n", counter);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
