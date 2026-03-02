#pragma once
/// @file esp32_event_handler.hpp
/// Desktop-side handler for events from the ESP32's SensorService.
/// Receives TEMPERATURE_CHANGED and BUTTON_PRESSED notifications.

#include <sero.hpp>
#include "demo_constants.hpp"

#include <cstdio>

namespace desktop_app {

class Esp32EventHandler : public sero::IEventHandler<Esp32EventHandler> {
public:
    void impl_on_event(uint16_t service_id, uint16_t event_id,
                       const uint8_t* payload, std::size_t payload_length) {
        if (service_id == demo::SENSOR_SERVICE_ID) {
            if (event_id == demo::SENSOR_EVT_TEMPERATURE && payload_length >= 4) {
                float temp = demo::read_float(payload);
                std::printf("[event] ESP32 temperature: %.2f°C\n", static_cast<double>(temp));
                last_temperature_ = temp;
                temp_event_count_++;
            } else if (event_id == demo::SENSOR_EVT_BUTTON && payload_length >= 4) {
                uint32_t count = demo::read_u32(payload);
                std::printf("[event] ESP32 button pressed (#%u)\n", count);
                button_event_count_++;
            }
        }
    }

    float    last_temperature()   const { return last_temperature_; }
    uint32_t temp_event_count()   const { return temp_event_count_; }
    uint32_t button_event_count() const { return button_event_count_; }

private:
    float    last_temperature_   = 0.0f;
    uint32_t temp_event_count_   = 0;
    uint32_t button_event_count_ = 0;
};

} // namespace desktop_app
