/// @file desktop_peer.cpp
/// Desktop Sero peer — combined provider + consumer for the ESP32 demo.
///
/// Provider side:
///   - TimeSyncService (0x3000): GET_TIME, PING, TIME_TICK event
///
/// Consumer side:
///   - Discovers ESP32's SensorService (0x1000) and EchoService (0x2000)
///   - Subscribes to TEMPERATURE_CHANGED and BUTTON_PRESSED events
///   - Periodically calls GET_TEMPERATURE, GET_UPTIME, SET_LED, ECHO, ADD, MULTIPLY
///   - ECHO/ADD/MULTIPLY calls go through HMAC-authenticated EchoService
///
/// Usage: ./desktop_peer [bind_ip]
///   bind_ip defaults to 0.0.0.0 (all interfaces)
///   For loopback testing: ./desktop_peer 127.0.0.1

#include "udp_transport.hpp"
#include "demo_constants.hpp"
#include "time_service.hpp"
#include "esp32_event_handler.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <csignal>

// ─── Configuration ──────────────────────────────────────────────

using Config  = sero::DefaultConfig;
using Runtime = sero::Runtime<example::UdpTransport, Config>;
using Addr    = sero::Address<Config>;

// ─── Application State ─────────────────────────────────────────

struct AppState {
    // Consumer — SensorService
    bool     sensor_found     = false;
    bool     sensor_subscribed = false;
    bool     was_sensor_sub   = false;

    // Consumer — EchoService
    bool     echo_found       = false;
    bool     echo_hmac_set    = false;

    // Timing
    uint32_t last_temp_req_ms  = 0;
    uint32_t last_uptime_req_ms = 0;
    uint32_t last_led_toggle_ms = 0;
    uint32_t last_echo_req_ms  = 0;
    uint32_t last_math_req_ms  = 0;
    uint32_t last_tick_ms      = 0;
    uint32_t last_diag_ms      = 0;

    // LED state for toggling
    bool     led_state         = false;
    uint32_t call_counter      = 0;

    Runtime* rt                = nullptr;
};

static AppState app;
static volatile bool running = true;

// ─── Signal Handler ─────────────────────────────────────────────

static void signal_handler(int) { running = false; }

// ─── Diagnostic Counter Names ───────────────────────────────────

static const char* diag_name(sero::DiagnosticCounter c) {
    switch (c) {
        case sero::DiagnosticCounter::CrcErrors:           return "CrcErrors";
        case sero::DiagnosticCounter::VersionMismatches:   return "VersionMismatches";
        case sero::DiagnosticCounter::OversizedPayloads:   return "OversizedPayloads";
        case sero::DiagnosticCounter::TypeIdMismatches:    return "TypeIdMismatches";
        case sero::DiagnosticCounter::DuplicateMessages:   return "DuplicateMessages";
        case sero::DiagnosticCounter::StaleMessages:       return "StaleMessages";
        case sero::DiagnosticCounter::AuthFailures:        return "AuthFailures";
        case sero::DiagnosticCounter::UnknownMessageTypes: return "UnknownMsgTypes";
        case sero::DiagnosticCounter::DroppedMessages:     return "DroppedMessages";
        default: return "Unknown";
    }
}

static void on_diagnostic(sero::DiagnosticCounter counter,
                           const uint8_t* /*header*/, void* /*ctx*/) {
    std::printf("[diag] %s incremented\n", diag_name(counter));
}

// ─── SD Callbacks ───────────────────────────────────────────────

static void on_service_found(uint16_t sid, const Addr& addr, void* /*ctx*/) {
    std::printf("[sd] Service 0x%04X found at %u.%u.%u.%u:%u\n",
                sid, addr[0], addr[1], addr[2], addr[3],
                unsigned((addr[4] << 8) | addr[5]));
    if (sid == demo::SENSOR_SERVICE_ID) app.sensor_found = true;
    if (sid == demo::ECHO_SERVICE_ID)   app.echo_found   = true;
}

static void on_service_lost(uint16_t sid, void* /*ctx*/) {
    std::printf("[sd] Service 0x%04X lost\n", sid);
    if (sid == demo::SENSOR_SERVICE_ID) {
        app.sensor_found     = false;
        app.sensor_subscribed = false;
    }
    if (sid == demo::ECHO_SERVICE_ID) {
        app.echo_found    = false;
        app.echo_hmac_set = false;
    }
}

static void on_subscription_ack(uint16_t sid, uint16_t eid,
                                 sero::ReturnCode rc, uint16_t ttl,
                                 void* /*ctx*/) {
    std::printf("[sd] Subscription ACK: 0x%04X/0x%04X rc=%u ttl=%u\n",
                sid, eid, static_cast<unsigned>(rc), ttl);
}

// ─── Request Callbacks ──────────────────────────────────────────

static void on_temperature_response(sero::ReturnCode rc,
                                     const uint8_t* payload, std::size_t len,
                                     void* /*ctx*/) {
    if (rc == sero::ReturnCode::E_OK && len >= 4) {
        float temp = demo::read_float(payload);
        std::printf("[client] GET_TEMPERATURE → %.2f°C\n", static_cast<double>(temp));
    } else {
        std::printf("[client] GET_TEMPERATURE failed: rc=%u\n", static_cast<unsigned>(rc));
    }
}

static void on_uptime_response(sero::ReturnCode rc,
                                const uint8_t* payload, std::size_t len,
                                void* /*ctx*/) {
    if (rc == sero::ReturnCode::E_OK && len >= 4) {
        uint32_t uptime = demo::read_u32(payload);
        std::printf("[client] GET_UPTIME → %u ms (%.1f min)\n",
                    uptime, uptime / 60000.0);
    } else {
        std::printf("[client] GET_UPTIME failed: rc=%u\n", static_cast<unsigned>(rc));
    }
}

static void on_echo_response(sero::ReturnCode rc,
                              const uint8_t* payload, std::size_t len,
                              void* /*ctx*/) {
    if (rc == sero::ReturnCode::E_OK) {
        std::printf("[client] ECHO → %zu bytes returned", len);
        if (len > 0 && len <= 32) {
            std::printf(": \"");
            for (std::size_t i = 0; i < len; i++) std::printf("%c", payload[i]);
            std::printf("\"");
        }
        std::printf("\n");
    } else {
        std::printf("[client] ECHO failed: rc=%u\n", static_cast<unsigned>(rc));
    }
}

struct MathCtx {
    const char* op;
    int32_t a, b;
};
static MathCtx math_ctx;

static void on_math_response(sero::ReturnCode rc,
                              const uint8_t* payload, std::size_t len,
                              void* ctx) {
    const auto* m = static_cast<MathCtx*>(ctx);
    if (rc == sero::ReturnCode::E_OK && len >= 4) {
        int32_t result = demo::read_i32(payload);
        std::printf("[client] %s(%d, %d) = %d\n", m->op, m->a, m->b, result);
    } else {
        std::printf("[client] %s(%d, %d) failed: rc=%u\n",
                    m->op, m->a, m->b, static_cast<unsigned>(rc));
    }
}

// ─── Print Diagnostics ─────────────────────────────────────────

static void print_diagnostics(Runtime& rt) {
    const auto& d = rt.diagnostics();
    std::printf("─── Diagnostic Counters ───\n");
    for (uint8_t i = 0; i < static_cast<uint8_t>(sero::DiagnosticCounter::_Count); i++) {
        auto c = static_cast<sero::DiagnosticCounter>(i);
        uint32_t val = d.get(c);
        if (val > 0) {
            std::printf("  %-20s : %u\n", diag_name(c), val);
        }
    }
    std::printf("  Pending requests     : %zu\n", rt.request_tracker().active_count());
    std::printf("───────────────────────────\n");
}

// ════════════════════════════════════════════════════════════════
//  main
// ════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    const char* bind_ip = "0.0.0.0";
    if (argc > 1) bind_ip = argv[1];

    std::printf("═══════════════════════════════════════\n");
    std::printf(" Sero Desktop Peer\n");
    std::printf("═══════════════════════════════════════\n\n");

    // ── Transport ───────────────────────────────────────────────
    example::UdpTransport transport;
    if (!transport.init(bind_ip, demo::DESKTOP_UNICAST_PORT)) {
        std::fprintf(stderr, "[ERROR] Transport init failed on %s:%u\n",
                     bind_ip, demo::DESKTOP_UNICAST_PORT);
        return 1;
    }
    std::printf("[transport] Bound to %s:%u\n", bind_ip, demo::DESKTOP_UNICAST_PORT);

    // ── Runtime ─────────────────────────────────────────────────
    Runtime rt(transport, demo::DESKTOP_CLIENT_ID);
    rt.set_local_address(transport.local_addr());
    app.rt = &rt;

    // ── Diagnostics ─────────────────────────────────────────────
    rt.set_diagnostic_callback(on_diagnostic, nullptr);

    // ── Provider: TimeSyncService ───────────────────────────────
    desktop_app::TimeSyncService time_svc;

    rt.register_service(demo::TIME_SERVICE_ID, time_svc,
                        demo::TIME_MAJOR_VERSION, demo::TIME_MINOR_VERSION,
                        /*auth_required=*/false);
    rt.register_event(demo::TIME_SERVICE_ID, demo::TIME_EVT_TICK);

    uint32_t now = example::now_ms();
    rt.offer_service(demo::TIME_SERVICE_ID, /*ttl=*/30, now);
    std::printf("[server] TimeSyncService 0x%04X offered\n", demo::TIME_SERVICE_ID);

    // ── Consumer: SD callbacks ──────────────────────────────────
    auto& sd = rt.sd_callbacks();
    sd.on_service_found    = on_service_found;
    sd.service_found_ctx   = nullptr;
    sd.on_service_lost     = on_service_lost;
    sd.service_lost_ctx    = nullptr;
    sd.on_subscription_ack = on_subscription_ack;
    sd.subscription_ack_ctx = nullptr;

    // ── Consumer: Find ESP32 services ───────────────────────────
    rt.find_service(demo::SENSOR_SERVICE_ID, demo::SENSOR_MAJOR_VERSION, now);
    rt.find_service(demo::ECHO_SERVICE_ID,   demo::ECHO_MAJOR_VERSION,   now);
    std::printf("[client] Searching for SensorService 0x%04X ...\n", demo::SENSOR_SERVICE_ID);
    std::printf("[client] Searching for EchoService   0x%04X ...\n", demo::ECHO_SERVICE_ID);

    // ── Event handler ───────────────────────────────────────────
    desktop_app::Esp32EventHandler esp32_evt_handler;

    // ── Initialize timing ───────────────────────────────────────
    app.last_temp_req_ms   = now;
    app.last_uptime_req_ms = now;
    app.last_led_toggle_ms = now;
    app.last_echo_req_ms   = now;
    app.last_math_req_ms   = now;
    app.last_tick_ms       = now;
    app.last_diag_ms       = now;

    if (!rt.enable_diagnostics(now)) {
        std::fprintf(stderr, "[main] Failed to enable diagnostics; diagnostics will be unavailable.\n");
    }
    std::printf("\n[main] Entering main loop (Ctrl+C to quit)\n\n");

    // ════════════════════════════════════════════════════════════
    //  Main loop
    // ════════════════════════════════════════════════════════════
    while (running) {
        now = example::now_ms();
        rt.process(now);

        // ── Provider: TIME_TICK event every 1 second ────────────
        if (now - app.last_tick_ms >= 1000) {
            app.last_tick_ms = now;
            uint64_t epoch = desktop_app::TimeSyncService::current_epoch();
            uint8_t payload[8];
            demo::write_u64(payload, epoch);
            if (rt.notify_event(demo::TIME_SERVICE_ID, demo::TIME_EVT_TICK,
                                payload, 8, now)) {
                // Only print occasionally to reduce noise
                static uint32_t tick_count = 0;
                if (++tick_count % 10 == 0)
                    std::printf("[server] TIME_TICK #%u (epoch=%llu)\n",
                                tick_count, (unsigned long long)epoch);
            }
        }

        // ── Consumer: SensorService interactions ────────────────
        if (app.sensor_found) {
            // Subscribe to events (once, or re-subscribe after reconnect)
            if (!app.sensor_subscribed) {
                if (app.was_sensor_sub) {
                    std::printf("[client] Re-subscribing (ESP32 restarted?)\n");
                }
                rt.subscribe_event(demo::SENSOR_SERVICE_ID, demo::SENSOR_EVT_TEMPERATURE,
                                   esp32_evt_handler, /*ttl=*/30, now);
                rt.subscribe_event(demo::SENSOR_SERVICE_ID, demo::SENSOR_EVT_BUTTON,
                                   esp32_evt_handler, /*ttl=*/30, now);
                app.sensor_subscribed = true;
                app.was_sensor_sub    = true;
                std::printf("[client] Subscribed to SensorService events\n");
            }

            // GET_TEMPERATURE every 8 seconds
            if (now - app.last_temp_req_ms >= 8000) {
                app.last_temp_req_ms = now;
                rt.request(demo::SENSOR_SERVICE_ID, demo::SENSOR_GET_TEMPERATURE,
                           nullptr, 0, on_temperature_response, nullptr, 2000, now);
                std::printf("[client] Sent GET_TEMPERATURE\n");
            }

            // GET_UPTIME every 12 seconds
            if (now - app.last_uptime_req_ms >= 12000) {
                app.last_uptime_req_ms = now;
                rt.request(demo::SENSOR_SERVICE_ID, demo::SENSOR_GET_UPTIME,
                           nullptr, 0, on_uptime_response, nullptr, 2000, now);
                std::printf("[client] Sent GET_UPTIME\n");
            }

            // SET_LED toggle (fire-and-forget) every 6 seconds
            if (now - app.last_led_toggle_ms >= 6000) {
                app.last_led_toggle_ms = now;
                app.led_state = !app.led_state;
                const uint8_t payload[1] = { app.led_state ? uint8_t(1) : uint8_t(0) };
                rt.fire_and_forget(demo::SENSOR_SERVICE_ID, demo::SENSOR_SET_LED,
                                   payload, 1);
                std::printf("[client] SET_LED → %s (fire-and-forget)\n",
                            app.led_state ? "ON" : "OFF");
            }
        } else {
            app.sensor_subscribed = false;
        }

        // ── Consumer: EchoService interactions (HMAC) ───────────
        if (app.echo_found) {
            // Set HMAC key for the ESP32 peer on first discovery
            if (!app.echo_hmac_set) {
                Addr peer_addr{};
                if (rt.service_discovery().get_provider_address(demo::ECHO_SERVICE_ID, peer_addr)) {
                    rt.set_hmac_key(peer_addr, demo::HMAC_KEY);
                    std::printf("[security] HMAC key set for ESP32 peer %u.%u.%u.%u:%u\n",
                                peer_addr[0], peer_addr[1],
                                peer_addr[2], peer_addr[3],
                                unsigned((peer_addr[4] << 8) | peer_addr[5]));
                    app.echo_hmac_set = true;
                }
            }

            // ECHO every 7 seconds
            if (now - app.last_echo_req_ms >= 7000) {
                app.last_echo_req_ms = now;
                const char* msg = "Hello from Desktop!";
                rt.request(demo::ECHO_SERVICE_ID, demo::ECHO_METHOD_ECHO,
                           reinterpret_cast<const uint8_t*>(msg), std::strlen(msg),
                           on_echo_response, nullptr, 2000, now);
                std::printf("[client] Sent ECHO (authenticated)\n");
            }

            // ADD or MULTIPLY every 5 seconds (alternating)
            if (now - app.last_math_req_ms >= 5000) {
                app.last_math_req_ms = now;
                app.call_counter++;

                int32_t a = static_cast<int32_t>(app.call_counter * 3);
                int32_t b = static_cast<int32_t>(app.call_counter + 7);

                uint8_t payload[8];
                demo::write_i32(payload, a);
                demo::write_i32(payload + 4, b);

                uint16_t method;
                if (app.call_counter % 2 == 1) {
                    method = demo::ECHO_METHOD_ADD;
                    math_ctx = {"ADD", a, b};
                } else {
                    method = demo::ECHO_METHOD_MULTIPLY;
                    math_ctx = {"MULTIPLY", a, b};
                }

                rt.request(demo::ECHO_SERVICE_ID, method,
                           payload, 8, on_math_response, &math_ctx, 2000, now);
                std::printf("[client] Sent %s(%d, %d) (authenticated)\n",
                            math_ctx.op, a, b);
            }
        } else {
            app.echo_hmac_set = false;
        }

        // ── Diagnostics every 30 seconds ────────────────────────
        if (now - app.last_diag_ms >= 30000) {
            app.last_diag_ms = now;
            print_diagnostics(rt);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::printf("\n[main] Shutting down...\n");
    rt.stop_offer(demo::TIME_SERVICE_ID);
    transport.shutdown();
    return 0;
}
