#pragma once
/// @file request_tracker.hpp
/// Pending request table with timeout eviction (§5.1, §10.2).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "sero/core/types.hpp"

namespace sero {

/// Application callback for request completion.
using RequestCallback = void (*)(ReturnCode rc,
                                 const uint8_t* payload, std::size_t payload_length,
                                 void* user_ctx);

template <typename Config>
class RequestTracker {
public:
    RequestTracker() = default;

    /// Allocate a new pending request slot.
    /// Returns the assigned request_id, or std::nullopt if table full.
    std::optional<uint32_t> allocate(uint16_t service_id,
                                     uint16_t method_id,
                                     uint32_t timeout_ms,
                                     uint32_t now_ms,
                                     RequestCallback callback,
                                     void* user_ctx)
    {
        for (std::size_t i = 0; i < Config::MaxPendingRequests; ++i) {
            if (!entries_[i].active) {
                uint32_t rid = next_request_id_++;
                auto& e          = entries_[i];
                e.request_id     = rid;
                e.service_id     = service_id;
                e.method_id      = method_id;
                e.deadline_ms    = now_ms + (timeout_ms > 0 ? timeout_ms : Config::RequestTimeoutMs);
                e.callback       = callback;
                e.user_ctx       = user_ctx;
                e.active         = true;
                return rid;
            }
        }
        return std::nullopt; // table full
    }

    /// Complete a request by matching request_id. Invokes callback, frees slot.
    /// Returns true if the request was found and completed.
    bool complete(uint32_t request_id, ReturnCode rc,
                  const uint8_t* payload, std::size_t payload_length)
    {
        for (std::size_t i = 0; i < Config::MaxPendingRequests; ++i) {
            if (entries_[i].active && entries_[i].request_id == request_id) {
                auto cb  = entries_[i].callback;
                auto ctx = entries_[i].user_ctx;
                entries_[i] = PendingEntry{}; // free slot
                if (cb) cb(rc, payload, payload_length, ctx);
                return true;
            }
        }
        return false;
    }

    /// Evict timed-out requests and invoke callbacks with E_TIMEOUT.
    void evict_expired(uint32_t now_ms) {
        for (std::size_t i = 0; i < Config::MaxPendingRequests; ++i) {
            if (entries_[i].active && time_after(now_ms, entries_[i].deadline_ms)) {
                auto cb  = entries_[i].callback;
                auto ctx = entries_[i].user_ctx;
                entries_[i] = PendingEntry{};
                if (cb) cb(ReturnCode::E_TIMEOUT, nullptr, 0, ctx);
            }
        }
    }

    std::size_t active_count() const {
        return static_cast<std::size_t>(std::count_if(
            entries_.begin(), entries_.begin() + Config::MaxPendingRequests,
            [](const PendingEntry& e) { return e.active; }));
    }

private:
    struct PendingEntry {
        uint32_t        request_id  = 0;
        uint16_t        service_id  = 0;
        uint16_t        method_id   = 0;
        uint32_t        deadline_ms = 0;
        RequestCallback callback    = nullptr;
        void*           user_ctx    = nullptr;
        bool            active      = false;
    };

    std::array<PendingEntry, Config::MaxPendingRequests> entries_{};
    uint32_t next_request_id_ = 1; // monotonically incrementing, wrapping

    static bool time_after(uint32_t now, uint32_t target) {
        return static_cast<int32_t>(now - target) > 0;
    }
};

} // namespace sero
