#pragma once
/// @file demo_constants.hpp
/// Shared constants for the ESP32 ↔ Desktop Sero demo.
/// Both sides include this to ensure agreement on service IDs, method IDs,
/// event IDs, and the pre-shared HMAC key.

#include <cstdint>

namespace demo {

// ─── Sensor Service (hosted on ESP32) ───────────────────────────

static constexpr uint16_t SENSOR_SERVICE_ID     = 0x1000;
static constexpr uint8_t  SENSOR_MAJOR_VERSION   = 1;
static constexpr uint8_t  SENSOR_MINOR_VERSION   = 0;

// Methods
static constexpr uint16_t SENSOR_GET_TEMPERATURE = 0x0001;  // → float32 (4 bytes)
static constexpr uint16_t SENSOR_GET_UPTIME      = 0x0002;  // → uint32 (4 bytes)
static constexpr uint16_t SENSOR_SET_LED         = 0x0003;  // fire-and-forget: uint8 on/off

// Events (bit 15 set)
static constexpr uint16_t SENSOR_EVT_TEMPERATURE = 0x8001;  // periodic float32
static constexpr uint16_t SENSOR_EVT_BUTTON      = 0x8002;  // uint32 press count

// ─── Echo Service (hosted on ESP32, HMAC-authenticated) ─────────

static constexpr uint16_t ECHO_SERVICE_ID        = 0x2000;
static constexpr uint8_t  ECHO_MAJOR_VERSION      = 1;
static constexpr uint8_t  ECHO_MINOR_VERSION      = 0;

// Methods
static constexpr uint16_t ECHO_METHOD_ECHO       = 0x0001;  // payload → same payload
static constexpr uint16_t ECHO_METHOD_ADD        = 0x0002;  // 2×int32 → int32
static constexpr uint16_t ECHO_METHOD_MULTIPLY   = 0x0003;  // 2×int32 → int32

// ─── Time Sync Service (hosted on Desktop) ──────────────────────

static constexpr uint16_t TIME_SERVICE_ID        = 0x3000;
static constexpr uint8_t  TIME_MAJOR_VERSION      = 1;
static constexpr uint8_t  TIME_MINOR_VERSION      = 0;

// Methods
static constexpr uint16_t TIME_GET_TIME          = 0x0001;  // → uint64 epoch secs (8 bytes)
static constexpr uint16_t TIME_PING              = 0x0002;  // → "pong" (4 bytes)

// Events (bit 15 set)
static constexpr uint16_t TIME_EVT_TICK          = 0x8001;  // periodic uint64 epoch secs

// ─── Network Configuration ──────────────────────────────────────

static constexpr uint16_t ESP32_UNICAST_PORT     = 30491;
static constexpr uint16_t DESKTOP_UNICAST_PORT   = 30492;
static constexpr uint16_t ESP32_CLIENT_ID        = 0x0100;
static constexpr uint16_t DESKTOP_CLIENT_ID      = 0x0200;

// ─── Pre-Shared HMAC Key (32 bytes) ────────────────────────────
// Both sides must use the same key for authenticated services.

// NOLINTNEXTLINE(cert-err58-cpp)
static constexpr uint8_t HMAC_KEY[32] = {
    0x53, 0x65, 0x72, 0x6F, 0x20, 0x44, 0x65, 0x6D,  // "Sero Dem"
    0x6F, 0x20, 0x4B, 0x65, 0x79, 0x20, 0x66, 0x6F,  // "o Key fo"
    0x72, 0x20, 0x48, 0x4D, 0x41, 0x43, 0x2D, 0x32,  // "r HMAC-2"
    0x35, 0x36, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,  // "56!!!!!!"
};

// ─── Payload Helpers ────────────────────────────────────────────

inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

inline uint32_t read_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

inline void write_i32(uint8_t* p, int32_t v) {
    write_u32(p, static_cast<uint32_t>(v));
}

inline int32_t read_i32(const uint8_t* p) {
    return static_cast<int32_t>(read_u32(p));
}

inline void write_u64(uint8_t* p, uint64_t v) {
    write_u32(p, static_cast<uint32_t>(v >> 32));
    write_u32(p + 4, static_cast<uint32_t>(v));
}

inline uint64_t read_u64(const uint8_t* p) {
    return (uint64_t(read_u32(p)) << 32) | uint64_t(read_u32(p + 4));
}

inline void write_float(uint8_t* p, float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    write_u32(p, u);
}

inline float read_float(const uint8_t* p) {
    uint32_t u = read_u32(p);
    float f;
    __builtin_memcpy(&f, &u, 4);
    return f;
}

} // namespace demo
