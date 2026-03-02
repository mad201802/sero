/// @file diag_scanner.cpp
/// Sero Diagnostic Scanner — discovers all diagnostics-enabled devices on the
/// network, queries their DTCs, counters, services, and device info, and
/// prints a live dashboard (like connecting to an OBD-II interface).
///
/// Usage: ./diag_scanner [bind_ip]
///   bind_ip defaults to 0.0.0.0 (all interfaces)
///
/// Optional arguments:
///   --clear <client_id_hex> [dtc_code_hex]
///      Clear DTCs on a specific device.  If dtc_code_hex is omitted or
///      set to FFFF, all DTCs are cleared.

#include "udp_transport.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <csignal>
#include <vector>

// ─── Configuration ──────────────────────────────────────────────

using Config  = sero::DefaultConfig;
using Runtime = sero::Runtime<example::UdpTransport, Config>;
using Addr    = sero::Address<Config>;

static constexpr uint16_t SCANNER_CLIENT_ID   = 0xFF00;
static constexpr uint16_t SCANNER_PORT        = 30493;
static constexpr uint32_t SCAN_INTERVAL_MS    = 10000;  // re-query every 10s
static constexpr uint32_t REQUEST_TIMEOUT_MS  = 2000;

// ─── Discovered Device ──────────────────────────────────────────

struct DeviceInfo {
    uint16_t client_id       = 0;
    uint32_t uptime_ms       = 0;
    uint8_t  protocol_version = 0;
    bool     info_received   = false;
};

struct DeviceDtc {
    uint16_t code            = 0;
    uint8_t  severity        = 0;
    uint8_t  status          = 0;
    uint32_t occurrence_count = 0;
    uint32_t first_seen_ms   = 0;
    uint32_t last_seen_ms    = 0;
};

struct DeviceService {
    uint16_t service_id   = 0;
    uint8_t  major        = 0;
    uint8_t  minor        = 0;
    bool     auth_required = false;
    bool     ready        = false;
};

struct DeviceCounters {
    uint32_t values[9] = {};
    bool     received  = false;
};

struct DiscoveredDevice {
    Addr     addr{};
    bool     found            = false;
    uint32_t last_query_ms    = 0;
    bool     queries_pending  = false;

    DeviceInfo                  info{};
    std::vector<DeviceDtc>      dtcs;
    std::vector<DeviceService>  services;
    DeviceCounters              counters;
};

static std::vector<DiscoveredDevice> devices;
static Runtime* g_rt = nullptr;
static volatile bool running = true;

// ─── Signal Handler ─────────────────────────────────────────────

static void signal_handler(int) { running = false; }

// ─── Helpers ────────────────────────────────────────────────────

static const char* severity_str(uint8_t sev) {
    switch (sev) {
        case 0: return "INF";
        case 1: return "WRN";
        case 2: return "ERR";
        case 3: return "FTL";
        default: return "???";
    }
}

static const char* counter_name(uint8_t idx) {
    static const char* names[] = {
        "CrcErrors", "VersionMismatches", "OversizedPayloads",
        "TypeIdMismatches", "DuplicateMessages", "StaleMessages",
        "AuthFailures", "UnknownMsgTypes", "DroppedMessages"
    };
    return idx < 9 ? names[idx] : "Unknown";
}

static void format_uptime(uint32_t ms, char* buf, std::size_t buf_size) {
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    mins %= 60;
    secs %= 60;
    std::snprintf(buf, buf_size, "%uh %um %us", hours, mins, secs);
}

static DiscoveredDevice* find_device_by_addr(const Addr& addr) {
    for (auto& dev : devices) {
        if (dev.addr == addr) return &dev;
    }
    return nullptr;
}

// ─── Response callbacks ─────────────────────────────────────────

static void on_device_info(sero::ReturnCode rc,
                           const uint8_t* payload, std::size_t len,
                           void* ctx)
{
    auto* dev = static_cast<DiscoveredDevice*>(ctx);
    if (rc == sero::ReturnCode::E_OK && len >= 8) {
        dev->info.client_id        = sero::MessageHeader::read_u16(payload);
        dev->info.uptime_ms        = sero::MessageHeader::read_u32(payload + 2);
        dev->info.protocol_version = payload[6];
        dev->info.info_received    = true;
    }
}

static void on_service_list(sero::ReturnCode rc,
                            const uint8_t* payload, std::size_t len,
                            void* ctx)
{
    auto* dev = static_cast<DiscoveredDevice*>(ctx);
    if (rc != sero::ReturnCode::E_OK || len < 2) return;

    uint16_t count = sero::MessageHeader::read_u16(payload);
    dev->services.clear();
    std::size_t offset = 2;
    for (uint16_t i = 0; i < count && offset + 6 <= len; ++i) {
        DeviceService svc;
        svc.service_id    = sero::MessageHeader::read_u16(payload + offset); offset += 2;
        svc.major         = payload[offset++];
        svc.minor         = payload[offset++];
        svc.auth_required = payload[offset++] != 0;
        svc.ready         = payload[offset++] != 0;
        dev->services.push_back(svc);
    }
}

static void on_dtcs(sero::ReturnCode rc,
                    const uint8_t* payload, std::size_t len,
                    void* ctx)
{
    auto* dev = static_cast<DiscoveredDevice*>(ctx);
    if (rc != sero::ReturnCode::E_OK || len < 2) return;

    uint16_t count = sero::MessageHeader::read_u16(payload);
    dev->dtcs.clear();
    std::size_t offset = 2;
    for (uint16_t i = 0; i < count && offset + 16 <= len; ++i) {
        DeviceDtc dtc;
        dtc.code             = sero::MessageHeader::read_u16(payload + offset); offset += 2;
        dtc.severity         = payload[offset++];
        dtc.status           = payload[offset++];
        dtc.occurrence_count = sero::MessageHeader::read_u32(payload + offset); offset += 4;
        dtc.first_seen_ms    = sero::MessageHeader::read_u32(payload + offset); offset += 4;
        dtc.last_seen_ms     = sero::MessageHeader::read_u32(payload + offset); offset += 4;
        dev->dtcs.push_back(dtc);
    }
}

static void on_counters(sero::ReturnCode rc,
                        const uint8_t* payload, std::size_t len,
                        void* ctx)
{
    auto* dev = static_cast<DiscoveredDevice*>(ctx);
    if (rc != sero::ReturnCode::E_OK || len < 36) return;

    for (int i = 0; i < 9; ++i) {
        dev->counters.values[i] = sero::MessageHeader::read_u32(payload + i * 4);
    }
    dev->counters.received = true;
}

static void on_clear_response(sero::ReturnCode rc,
                              const uint8_t* /*payload*/, std::size_t /*len*/,
                              void* /*ctx*/)
{
    if (rc == sero::ReturnCode::E_OK) {
        std::printf("[scanner] DTCs cleared successfully\n");
    } else {
        std::printf("[scanner] DTC clear failed: rc=%u\n", static_cast<unsigned>(rc));
    }
}

// ─── SD Callback ────────────────────────────────────────────────

static void on_diag_service_found(uint16_t sid, const Addr& addr, void* /*ctx*/) {
    if (sid != sero::DIAG_SERVICE_ID) return;

    // Already known?
    if (find_device_by_addr(addr)) return;

    DiscoveredDevice dev;
    dev.addr  = addr;
    dev.found = true;
    devices.push_back(dev);

    std::printf("[scanner] Discovered diagnostics device at %u.%u.%u.%u:%u\n",
                addr[0], addr[1], addr[2], addr[3],
                unsigned((addr[4] << 8) | addr[5]));
}

static void on_diag_service_lost(uint16_t sid, void* /*ctx*/) {
    if (sid != sero::DIAG_SERVICE_ID) return;
    std::printf("[scanner] Diagnostics service 0x%04X lost\n", sid);
    // Mark devices as not found (they'll be rediscovered if they come back)
    for (auto& dev : devices) {
        dev.found = false;
    }
}

// ─── Query a device ─────────────────────────────────────────────

static void query_device(DiscoveredDevice& dev, uint32_t now_ms) {
    using DM = sero::DiagMethod;

    g_rt->request(sero::DIAG_SERVICE_ID,
                  static_cast<uint16_t>(DM::DIAG_GET_DEVICE_INFO),
                  nullptr, 0, on_device_info, &dev, REQUEST_TIMEOUT_MS, now_ms);

    g_rt->request(sero::DIAG_SERVICE_ID,
                  static_cast<uint16_t>(DM::DIAG_GET_SERVICE_LIST),
                  nullptr, 0, on_service_list, &dev, REQUEST_TIMEOUT_MS, now_ms);

    g_rt->request(sero::DIAG_SERVICE_ID,
                  static_cast<uint16_t>(DM::DIAG_GET_DTCS),
                  nullptr, 0, on_dtcs, &dev, REQUEST_TIMEOUT_MS, now_ms);

    g_rt->request(sero::DIAG_SERVICE_ID,
                  static_cast<uint16_t>(DM::DIAG_GET_COUNTERS),
                  nullptr, 0, on_counters, &dev, REQUEST_TIMEOUT_MS, now_ms);

    dev.last_query_ms = now_ms;
}

// ─── Print dashboard ────────────────────────────────────────────

static void print_dashboard() {
    std::printf("\n\033[2J\033[H");  // clear screen
    std::printf("═══════════════════════════════════════════════════════════\n");
    std::printf(" Sero Diagnostic Scanner  —  %zu device(s) discovered\n",
                devices.size());
    std::printf("═══════════════════════════════════════════════════════════\n");

    for (const auto& dev : devices) {
        if (!dev.found) continue;

        char uptime_buf[64];
        format_uptime(dev.info.uptime_ms, uptime_buf, sizeof(uptime_buf));

        std::printf("\n┌─── Device 0x%04X (%u.%u.%u.%u:%u) ",
                    dev.info.client_id,
                    dev.addr[0], dev.addr[1], dev.addr[2], dev.addr[3],
                    unsigned((dev.addr[4] << 8) | dev.addr[5]));
        if (dev.info.info_received) {
            std::printf("uptime: %s  proto: v%u", uptime_buf, dev.info.protocol_version);
        }
        std::printf("\n");

        // Services
        std::printf("│ Services: ");
        if (dev.services.empty()) {
            std::printf("(none)\n");
        } else {
            for (std::size_t i = 0; i < dev.services.size(); ++i) {
                const auto& s = dev.services[i];
                if (i > 0) std::printf(", ");
                std::printf("0x%04X v%u.%u", s.service_id, s.major, s.minor);
                if (s.auth_required) std::printf("[auth]");
                std::printf(s.ready ? "[ready]" : "[not-ready]");
            }
            std::printf("\n");
        }

        // DTCs
        if (dev.dtcs.empty()) {
            std::printf("│ DTCs: (none)\n");
        } else {
            std::printf("│ DTCs: %zu active\n", dev.dtcs.size());
            for (const auto& dtc : dev.dtcs) {
                char age_buf[32];
                if (dev.info.info_received && dev.info.uptime_ms > dtc.last_seen_ms) {
                    format_uptime(dev.info.uptime_ms - dtc.last_seen_ms, age_buf, sizeof(age_buf));
                } else {
                    std::snprintf(age_buf, sizeof(age_buf), "t=%u", dtc.last_seen_ms);
                }
                std::printf("│   [%s] 0x%04X  (count: %u, last: %s ago)\n",
                            severity_str(dtc.severity), dtc.code,
                            dtc.occurrence_count, age_buf);
            }
        }

        // Counters
        if (dev.counters.received) {
            std::printf("│ Counters:");
            bool any = false;
            for (int i = 0; i < 9; ++i) {
                if (dev.counters.values[i] > 0) {
                    std::printf(" %s=%u", counter_name(static_cast<uint8_t>(i)),
                                dev.counters.values[i]);
                    any = true;
                }
            }
            if (!any) std::printf(" (all zero)");
            std::printf("\n");
        }

        std::printf("└───────────────────────────────────────────────────────\n");
    }
    std::printf("\n[Press Ctrl+C to quit. Refreshing every %u seconds.]\n",
                SCAN_INTERVAL_MS / 1000);
}

// ════════════════════════════════════════════════════════════════
//  main
// ════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    const char* bind_ip = "0.0.0.0";
    uint16_t clear_target_client = 0;
    uint16_t clear_dtc_code = 0xFFFF;
    bool do_clear = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--clear") == 0 && i + 1 < argc) {
            do_clear = true;
            clear_target_client = static_cast<uint16_t>(
                std::strtoul(argv[++i], nullptr, 16));
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                clear_dtc_code = static_cast<uint16_t>(
                    std::strtoul(argv[++i], nullptr, 16));
            }
        } else if (argv[i][0] != '-') {
            bind_ip = argv[i];
        }
    }

    std::printf("═══════════════════════════════════════\n");
    std::printf(" Sero Diagnostic Scanner\n");
    std::printf("═══════════════════════════════════════\n\n");

    // ── Transport ───────────────────────────────────────────────
    example::UdpTransport transport;
    if (!transport.init(bind_ip, SCANNER_PORT)) {
        std::fprintf(stderr, "[ERROR] Transport init failed on %s:%u\n",
                     bind_ip, SCANNER_PORT);
        return 1;
    }
    std::printf("[transport] Bound to %s:%u\n", bind_ip, SCANNER_PORT);

    // ── Runtime ─────────────────────────────────────────────────
    Runtime rt(transport, SCANNER_CLIENT_ID);
    rt.set_local_address(transport.local_addr());
    g_rt = &rt;

    // ── SD callbacks ────────────────────────────────────────────
    auto& sd = rt.sd_callbacks();
    sd.on_service_found  = on_diag_service_found;
    sd.service_found_ctx = nullptr;
    sd.on_service_lost   = on_diag_service_lost;
    sd.service_lost_ctx  = nullptr;

    // ── Find all diagnostics services ───────────────────────────
    uint32_t now = example::now_ms();
    rt.find_service(sero::DIAG_SERVICE_ID, 1, now);
    std::printf("[scanner] Searching for diagnostics services (0x%04X)...\n\n",
                sero::DIAG_SERVICE_ID);

    uint32_t last_scan_ms = 0;

    // ── Main loop ───────────────────────────────────────────────
    while (running) {
        now = example::now_ms();
        rt.process(now);

        // Query discovered devices periodically
        if (now - last_scan_ms >= SCAN_INTERVAL_MS) {
            last_scan_ms = now;

            for (auto& dev : devices) {
                if (dev.found) {
                    query_device(dev, now);
                }
            }

            // Handle --clear command (one-shot after first scan)
            if (do_clear) {
                // We need at least one device discovered
                for (const auto& dev : devices) {
                    if (dev.found && dev.info.info_received &&
                        dev.info.client_id == clear_target_client) {
                        uint8_t payload[2];
                        sero::MessageHeader::write_u16(payload, clear_dtc_code);
                        rt.request(sero::DIAG_SERVICE_ID,
                                   static_cast<uint16_t>(sero::DiagMethod::DIAG_CLEAR_DTCS),
                                   payload, 2, on_clear_response, nullptr,
                                   REQUEST_TIMEOUT_MS, now);
                        std::printf("[scanner] Sent CLEAR_DTCS to device 0x%04X (code=0x%04X)\n",
                                    clear_target_client, clear_dtc_code);
                        do_clear = false;
                        break;
                    }
                }
            }

            // Print after a brief delay to let responses arrive
        }

        // Print dashboard shortly after query responses should have arrived
        if (last_scan_ms > 0 && now - last_scan_ms > 500 &&
            now - last_scan_ms < 600) {
            print_dashboard();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::printf("\n[scanner] Shutting down...\n");
    transport.shutdown();
    return 0;
}
