/// @file client.cpp
/// Example Sero client – discovers Calculator, subscribes to Counter,
/// calls Add and Multiply alternately.

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

static constexpr uint16_t CLIENT_PORT = 9001;
static constexpr uint16_t MY_CLIENT_ID = 0x0002;

// ── Client event handler (receives Counter notifications) ───────

class CounterHandler : public sero::IEventHandler<CounterHandler> {
public:
    void impl_on_event(uint16_t /*service_id*/, uint16_t event_id,
                       const uint8_t* payload, std::size_t payload_length) {
        if (event_id == EVENT_CTR && payload_length >= 4) {
            uint32_t v = (uint32_t(payload[0]) << 24) |
                         (uint32_t(payload[1]) << 16) |
                         (uint32_t(payload[2]) <<  8) |
                          uint32_t(payload[3]);
            std::printf("[client] Counter event received: %u\n", v);
        }
    }
};

// ── Helpers ─────────────────────────────────────────────────────

static void write_i32(uint8_t* p, int32_t v) {
    auto u = static_cast<uint32_t>(v);
    p[0] = static_cast<uint8_t>(u >> 24);
    p[1] = static_cast<uint8_t>(u >> 16);
    p[2] = static_cast<uint8_t>(u >>  8);
    p[3] = static_cast<uint8_t>(u);
}

static int32_t read_i32(const uint8_t* p) {
    return static_cast<int32_t>(
        (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) <<  8) |  uint32_t(p[3]));
}

// ── Request completion callback ─────────────────────────────────

struct CallContext {
    const char* name;
    int32_t a;
    int32_t b;
};

static void on_response(sero::ReturnCode rc,
                         const uint8_t* resp, std::size_t resp_len,
                         void* user_ctx) {
    const auto* ctx = static_cast<CallContext*>(user_ctx);
    if (rc == sero::ReturnCode::E_OK && resp_len >= 4) {
        int32_t result = read_i32(resp);
        std::printf("[client] %s(%d, %d) = %d\n", ctx->name, ctx->a, ctx->b, result);
    } else {
        std::printf("[client] %s(%d, %d) failed, rc=%u\n",
                    ctx->name, ctx->a, ctx->b, static_cast<unsigned>(rc));
    }
}

// ── main ────────────────────────────────────────────────────────

int main() {
    example::UdpTransport transport;
    if (!transport.init("127.0.0.1", CLIENT_PORT)) return 1;

    Runtime rt(transport, MY_CLIENT_ID);
    rt.set_local_address(transport.local_addr());

    // SD callbacks
    struct AppState {
        bool  service_found = false;
        Runtime* rt         = nullptr;
    };
    AppState app;
    app.rt = &rt;

    CounterHandler counter_handler;

    auto& sd = rt.sd_callbacks();

    sd.on_service_found = [](uint16_t sid, const Addr& addr, void* ctx) {
        auto* s = static_cast<AppState*>(ctx);
        if (sid == SERVICE_ID) {
            s->service_found = true;
            std::printf("[client] Service 0x%04X found at %u.%u.%u.%u:%u\n",
                        sid, addr[0], addr[1], addr[2], addr[3],
                        unsigned((addr[4] << 8) | addr[5]));
        }
    };
    sd.service_found_ctx = &app;

    sd.on_service_lost = [](uint16_t sid, void* ctx) {
        auto* s = static_cast<AppState*>(ctx);
        if (sid == SERVICE_ID) {
            s->service_found = false;
            std::printf("[client] Service 0x%04X lost\n", sid);
        }
    };
    sd.service_lost_ctx = &app;

    // Track whether service was previously subscribed so we can
    // log re-subscription after provider restart.
    bool was_subscribed = false;

    sd.on_subscription_ack = [](uint16_t sid, uint16_t eid,
                                 sero::ReturnCode rc, uint16_t ttl, void*) {
        std::printf("[client] Subscription ACK for 0x%04X/0x%04X rc=%u ttl=%u\n",
                    sid, eid, static_cast<unsigned>(rc), ttl);
    };
    sd.subscription_ack_ctx = nullptr;

    // Start searching for the service
    uint32_t now = example::now_ms();
    rt.find_service(SERVICE_ID, /*major_version=*/1, now);
    std::printf("[client] Searching for service 0x%04X ...\n", SERVICE_ID);

    uint32_t last_call_ms = now;
    uint32_t call_counter = 0;

    // Small pool of call contexts (we only ever have one outstanding at a time)
    static CallContext call_ctx;
    bool subscribed = false;

    // ── Run loop ────────────────────────────────────────────────
    for (;;) {
        now = example::now_ms();
        rt.process(now);

        // Once found, subscribe to events (first time or after reconnect)
        if (app.service_found && !subscribed) {
            if (was_subscribed) {
                std::printf("[client] Re-subscribing to Counter event (provider restarted)\n");
            }
            subscribed = true;
            was_subscribed = true;
            rt.subscribe_event(SERVICE_ID, EVENT_CTR, counter_handler,
                               /*ttl_seconds=*/30, now);
            std::printf("[client] Subscribed to Counter event\n");
        }
        if (!app.service_found) {
            subscribed = false;
        }

        // Alternate Add / Multiply calls every 3 seconds
        if (app.service_found && (now - last_call_ms >= 3000)) {
            last_call_ms = now;
            ++call_counter;

            int32_t a = static_cast<int32_t>(call_counter);
            int32_t b = static_cast<int32_t>(call_counter + 10);

            uint8_t payload[8];
            write_i32(payload, a);
            write_i32(payload + 4, b);

            uint16_t method = (call_counter % 2 == 1) ? METHOD_ADD : METHOD_MUL;
            call_ctx.name = (method == METHOD_ADD) ? "Add" : "Mul";
            call_ctx.a = a;
            call_ctx.b = b;

            std::printf("[client] Calling %s(%d, %d) ...\n", call_ctx.name, a, b);

            rt.request(SERVICE_ID, method, payload, 8,
                       on_response, &call_ctx,
                       /*timeout_ms=*/1000, now);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
