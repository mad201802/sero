#pragma once
/// @file config.hpp
/// Compile-time configuration for Sero (§8).
/// Users provide their own struct with the same static constexpr members
/// and pass it as a template parameter to Runtime.

#include <cstddef>
#include <cstdint>

namespace sero {

struct DefaultConfig {
    static constexpr std::size_t MaxPayloadSize           = 1400;
    static constexpr std::size_t MaxServices               = 16;
    static constexpr std::size_t MaxMethods                = 32;
    static constexpr std::size_t MaxEvents                 = 16;
    static constexpr std::size_t MaxSubscribers            = 8;
    static constexpr std::size_t MaxPendingRequests        = 16;
    static constexpr std::size_t MaxKnownServices          = 32;
    static constexpr uint32_t    RequestTimeoutMs          = 1000;
    static constexpr uint16_t    OfferTtlSeconds           = 5;
    static constexpr uint16_t    SubscriptionTtlSeconds    = 10;
    static constexpr uint8_t     SdFindRetryCount          = 3;
    static constexpr uint32_t    SdFindInitialDelayMs      = 100;
    static constexpr uint8_t     SdFindBackoffMultiplier   = 2;
    static constexpr uint32_t    SdFindJitterMs            = 50;
    static constexpr uint8_t     SeqCounterAcceptWindow    = 15;
    static constexpr std::size_t TransportAddressSize      = 8;
    static constexpr std::size_t MaxReceiveQueueSize       = 16;
    static constexpr std::size_t MaxTrackedPeers           = 32;
    static constexpr std::size_t HmacKeySize               = 32;
};

} // namespace sero
