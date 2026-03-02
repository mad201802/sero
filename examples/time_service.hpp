#pragma once
/// @file time_service.hpp
/// Desktop Time Sync Service — provides epoch time and ping.
/// Service ID: 0x3000

#include <sero.hpp>
#include "demo_constants.hpp"

#include <cstdio>
#include <ctime>
#include <cstring>

namespace desktop_app {

class TimeSyncService : public sero::IService<TimeSyncService> {
public:
    bool impl_is_ready() const { return true; }

    sero::ReturnCode impl_on_request(
        uint16_t       method_id,
        const uint8_t* /*payload*/, std::size_t /*payload_length*/,
        uint8_t*       response, std::size_t& response_length)
    {
        switch (method_id) {
        case demo::TIME_GET_TIME:
            return handle_get_time(response, response_length);
        case demo::TIME_PING:
            return handle_ping(response, response_length);
        default:
            response_length = 0;
            return sero::ReturnCode::E_UNKNOWN_METHOD;
        }
    }

    /// Get current epoch seconds (for use in event notifications).
    static uint64_t current_epoch() {
        return static_cast<uint64_t>(std::time(nullptr));
    }

private:
    sero::ReturnCode handle_get_time(uint8_t* response, std::size_t& response_length) {
        uint64_t epoch = current_epoch();
        demo::write_u64(response, epoch);
        response_length = 8;
        std::printf("[time] GET_TIME → %llu\n", (unsigned long long)epoch);
        return sero::ReturnCode::E_OK;
    }

    sero::ReturnCode handle_ping(uint8_t* response, std::size_t& response_length) {
        std::memcpy(response, "pong", 4);
        response_length = 4;
        std::printf("[time] PING → pong\n");
        return sero::ReturnCode::E_OK;
    }
};

} // namespace desktop_app
