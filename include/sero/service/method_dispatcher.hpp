#pragma once
/// @file method_dispatcher.hpp
/// Route incoming requests to registered service handlers (§5, §10.2).

#include <array>
#include <cstddef>
#include <cstdint>

#include "sero/core/types.hpp"
#include "sero/service/service.hpp"

namespace sero {

template <typename Config>
class MethodDispatcher {
public:
    MethodDispatcher() = default;

    /// Register a service. Returns false if table full or duplicate ID.
    bool register_service(const ServiceEntry& entry) {
        // Check for duplicate
        for (std::size_t i = 0; i < count_; ++i) {
            if (services_[i].active && services_[i].service_id == entry.service_id) {
                return false;
            }
        }
        if (count_ >= Config::MaxServices) return false;
        services_[count_] = entry;
        ++count_;
        return true;
    }

    /// Unregister a service by ID. Returns false if not found.
    bool unregister_service(uint16_t service_id) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (services_[i].active && services_[i].service_id == service_id) {
                // Move last into this slot to keep array compact
                if (i != count_ - 1) services_[i] = services_[count_ - 1];
                services_[count_ - 1] = ServiceEntry{};
                --count_;
                return true;
            }
        }
        return false;
    }

    /// Dispatch a request to the appropriate service handler.
    /// @return The ReturnCode indicating the result.
    ReturnCode dispatch(uint16_t service_id, uint16_t method_id,
                        const uint8_t* payload, std::size_t payload_length,
                        uint8_t* response, std::size_t& response_length) const
    {
        const ServiceEntry* svc = find(service_id);
        if (!svc) return ReturnCode::E_UNKNOWN_SERVICE;
        if (!svc->is_ready_fn(svc->context)) return ReturnCode::E_NOT_READY;
        return svc->on_request_fn(svc->context, method_id,
                                  payload, payload_length,
                                  response, response_length);
    }

    /// Look up a service entry (for auth policy checks etc.)
    const ServiceEntry* find(uint16_t service_id) const {
        for (std::size_t i = 0; i < count_; ++i) {
            if (services_[i].active && services_[i].service_id == service_id) {
                return &services_[i];
            }
        }
        return nullptr;
    }

    std::size_t count() const { return count_; }

    /// Iterate over all active services (for SD offers).
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t i = 0; i < count_; ++i) {
            if (services_[i].active) fn(services_[i]);
        }
    }

private:
    std::array<ServiceEntry, Config::MaxServices> services_{};
    std::size_t count_ = 0;
};

} // namespace sero
