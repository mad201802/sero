# Sero

**Embedded-first, realtime-ready service-oriented communication for C++17**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![CMake](https://img.shields.io/badge/build-CMake%203.14+-064F8C.svg)](https://cmake.org/)

Sero is a header-only C++17 library that provides service discovery, remote method invocation, and event notification for resource-constrained MCUs and safety-critical environments. It is designed from the ground up for deterministic, real-time embedded systems — **zero heap allocations, no mutexes, no exceptions, no RTTI**.

---

## Table of Contents

- [Features](#features)
- [Design Principles](#design-principles)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
  - [Requirements](#requirements)
  - [Installation](#installation)
  - [CMake Integration](#cmake-integration)
- [Quick Start](#quick-start)
  - [Defining a Service (Provider)](#defining-a-service-provider)
  - [Calling a Service (Consumer)](#calling-a-service-consumer)
  - [Event Subscription](#event-subscription)
  - [Implementing a Transport](#implementing-a-transport)
- [Configuration](#configuration)
- [API Overview](#api-overview)
  - [Runtime](#runtime)
  - [IService](#iservice)
  - [ITransport](#itransport)
  - [IEventHandler](#ieventhandler)
- [Security](#security)
- [Diagnostics](#diagnostics)
- [Examples](#examples)
- [Testing](#testing)
- [Protocol Specification](#protocol-specification)
- [Project Structure](#project-structure)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- **Service Discovery** — automatic offer/find, subscribe/unsubscribe with TTL management and retry logic
- **Remote Method Invocation** — request/response with timeout tracking and correlation IDs
- **Event Notifications** — publish/subscribe with per-subscriber fan-out
- **End-to-End Protection** — CRC-16 integrity checks and sequence counter validation
- **Message Authentication** — HMAC-SHA256-128 per-peer authentication with pre-shared keys
- **Diagnostic Counters** — 9 built-in counters with optional application callbacks
- **Header-Only** — single `#include <sero.hpp>` to use the entire library
- **Fully Configurable** — all resource limits are compile-time `constexpr` template parameters

---

## Design Principles

| Principle | Detail |
|---|---|
| **Zero heap allocation** | All buffers and tables are compile-time sized |
| **No mutexes** | Run-to-completion execution model; no priority inversion |
| **No exceptions, no RTTI** | Defensive checks only; compiles with `-fno-exceptions -fno-rtti` |
| **Bounded time and space** | Every operation has a compile-time upper bound |
| **CRTP interfaces** | Zero-overhead polymorphism; users provide concrete implementations at compile time |
| **C++17** | Uses `constexpr if`, fold expressions, structured bindings, `std::optional` |

---

## Architecture

Sero is organized into three layers:

```
┌──────────────────────────────────────────────────┐
│                    Runtime                        │
│   (top-level coordinator, process() loop)        │
├──────────┬────────────┬──────────┬───────────────┤
│  Method  │   Event    │ Request  │    Service    │
│Dispatcher│  Manager   │ Tracker  │  Discovery    │
├──────────┴────────────┴──────────┴───────────────┤
│  E2E Protection │ Message Authenticator │ Diag.  │
├─────────────────┴───────────────────────┴────────┤
│        ITransport (user-provided, CRTP)          │
└──────────────────────────────────────────────────┘
```

| Component | Responsibility |
|---|---|
| `Runtime` | Top-level coordinator; drives the `process()` cycle |
| `MethodDispatcher` | Routes incoming requests to registered service handlers |
| `EventManager` | Manages subscriber tables, TTL eviction, notification fan-out |
| `RequestTracker` | Tracks pending outbound requests with timeout eviction |
| `ServiceDiscovery` | SD state machines, offer/find protocols, TTL management |
| `E2EProtection` | CRC-16 integrity and sequence counter validation |
| `MessageAuthenticator` | HMAC-SHA256-128 compute/verify with per-peer keys |
| `DiagnosticCounters` | Counter storage and application callback dispatch |

---

## Getting Started

### Requirements

- **C++17** compatible compiler (GCC 7+, Clang 5+, MSVC 19.14+)
- **CMake 3.14+** (for building examples and tests)
- No external runtime dependencies

### Installation

```bash
git clone https://github.com/your-username/sero.git
cd sero
```

Since Sero is header-only, you can simply copy the `include/` directory into your project, or use one of the CMake integration methods below.

### CMake Integration

#### Option 1: `add_subdirectory`

```cmake
add_subdirectory(sero)
target_link_libraries(your_target PRIVATE sero)
```

#### Option 2: `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
    sero
    GIT_REPOSITORY https://github.com/your-username/sero.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(sero)
target_link_libraries(your_target PRIVATE sero)
```

#### Option 3: Install & `find_package`

```bash
cmake -B build -DBUILD_TESTING=OFF
cmake --install build --prefix /usr/local
```

Then in your project:

```cmake
find_package(sero REQUIRED)
target_link_libraries(your_target PRIVATE sero)
```

---

## Quick Start

### Defining a Service (Provider)

Implement the `IService` CRTP interface:

```cpp
#include <sero.hpp>

class Calculator : public sero::IService<Calculator> {
public:
    bool impl_is_ready() const { return true; }

    sero::ReturnCode impl_on_request(
        uint16_t method_id,
        const uint8_t* payload, std::size_t payload_length,
        uint8_t* response, std::size_t& response_length)
    {
        if (method_id == 0x0001) {
            // Add: read two int32s, return their sum
            int32_t a = read_be32(payload);
            int32_t b = read_be32(payload + 4);
            write_be32(response, a + b);
            response_length = 4;
            return sero::ReturnCode::E_OK;
        }
        response_length = 0;
        return sero::ReturnCode::E_UNKNOWN_METHOD;
    }
};
```

Register and offer the service:

```cpp
MyTransport transport;
sero::Runtime<MyTransport> rt(transport, /*client_id=*/0x0001);
rt.set_local_address(transport.local_addr());

Calculator calc;
rt.register_service(/*service_id=*/0x0100, calc, /*major=*/1, /*minor=*/0);
rt.offer_service(0x0100, /*ttl_seconds=*/30, now_ms());

// Main loop
while (true) {
    rt.process(now_ms());
    sleep_ms(10);
}
```

### Calling a Service (Consumer)

```cpp
// Discover the service
rt.find_service(0x0100, /*major_version=*/1, now_ms());

// Once found (via SD callback), make a request
uint8_t payload[8];
write_be32(payload, 10);
write_be32(payload + 4, 20);

rt.request(
    /*service_id=*/0x0100,
    /*method_id=*/0x0001,
    payload, 8,
    [](sero::ReturnCode rc, const uint8_t* resp, std::size_t len, void*) {
        if (rc == sero::ReturnCode::E_OK) {
            int32_t result = read_be32(resp); // 30
        }
    },
    nullptr, /*timeout_ms=*/1000, now_ms()
);
```

### Event Subscription

Define an event handler:

```cpp
class CounterHandler : public sero::IEventHandler<CounterHandler> {
public:
    void impl_on_event(uint16_t service_id, uint16_t event_id,
                       const uint8_t* payload, std::size_t payload_length) {
        // Handle notification
    }
};
```

Subscribe on the consumer side:

```cpp
CounterHandler handler;
rt.subscribe_event(0x0100, 0x8001, handler, /*ttl_seconds=*/30, now_ms());
```

Publish on the provider side:

```cpp
rt.register_event(0x0100, 0x8001);
rt.notify_event(0x0100, 0x8001, payload, payload_length, now_ms());
```

### Implementing a Transport

Implement the `ITransport` CRTP interface for your platform:

```cpp
class MyTransport : public sero::ITransport<MyTransport, sero::DefaultConfig> {
public:
    bool impl_send(const Addr& dest, const uint8_t* data, std::size_t len) {
        // Send data to destination address
    }

    bool impl_broadcast(const uint8_t* data, std::size_t len) {
        // Broadcast data (used for service discovery)
    }

    bool impl_poll(Addr& source, const uint8_t*& data, std::size_t& len) {
        // Dequeue next received message, return false if queue empty
        // Returned data pointer must remain valid until the next poll() call
    }
};
```

See [examples/udp_transport.hpp](examples/udp_transport.hpp) for a complete UDP/multicast implementation.

---

## Configuration

All resource limits are compile-time constants. Provide a custom config struct to tune for your platform:

```cpp
struct MyConfig {
    static constexpr std::size_t MaxPayloadSize        = 256;   // smaller for CAN/LIN
    static constexpr std::size_t MaxServices            = 4;
    static constexpr std::size_t MaxMethods             = 8;
    static constexpr std::size_t MaxEvents              = 4;
    static constexpr std::size_t MaxSubscribers         = 4;
    static constexpr std::size_t MaxPendingRequests     = 8;
    static constexpr std::size_t MaxKnownServices       = 8;
    static constexpr uint32_t    RequestTimeoutMs       = 500;
    static constexpr uint16_t    OfferTtlSeconds        = 5;
    static constexpr uint16_t    SubscriptionTtlSeconds = 10;
    static constexpr uint8_t     SdFindRetryCount       = 3;
    static constexpr uint32_t    SdFindInitialDelayMs   = 100;
    static constexpr uint8_t     SdFindBackoffMultiplier= 2;
    static constexpr uint32_t    SdFindJitterMs         = 50;
    static constexpr uint8_t     SeqCounterAcceptWindow = 15;
    static constexpr std::size_t TransportAddressSize   = 8;
    static constexpr std::size_t MaxReceiveQueueSize    = 16;
    static constexpr std::size_t MaxTrackedPeers        = 8;
    static constexpr std::size_t HmacKeySize            = 32;
};

sero::Runtime<MyTransport, MyConfig> rt(transport, client_id);
```

| Parameter | Default | Description |
|---|---|---|
| `MaxPayloadSize` | 1400 | Maximum payload bytes per message |
| `MaxServices` | 16 | Maximum hosted services per device |
| `MaxMethods` | 32 | Maximum methods per service |
| `MaxEvents` | 16 | Maximum events per service |
| `MaxSubscribers` | 8 | Maximum subscribers per event |
| `MaxPendingRequests` | 16 | Maximum in-flight client requests |
| `MaxKnownServices` | 32 | Maximum remote services tracked |
| `RequestTimeoutMs` | 1000 | Default request timeout (ms) |
| `TransportAddressSize` | 8 | Fixed transport address size (bytes) |
| `MaxReceiveQueueSize` | 16 | Transport receive ring buffer capacity |
| `MaxTrackedPeers` | 32 | Maximum peers tracked for E2E validation |
| `HmacKeySize` | 32 | Pre-shared HMAC key size (bytes) |

---

## API Overview

### Runtime

The central coordinator. Call `process()` from your main loop or RTOS task.

```cpp
template <typename TransportImpl, typename Config = DefaultConfig>
class Runtime {
    Runtime(TransportImpl& transport, uint16_t client_id);

    void process(uint32_t now_ms);                          // Main processing cycle

    // Provider
    bool register_service(uint16_t id, IService<Impl>& svc, uint8_t major, uint8_t minor);
    bool offer_service(uint16_t id, uint16_t ttl_seconds, uint32_t now_ms);
    void stop_offer(uint16_t id);
    bool register_event(uint16_t service_id, uint16_t event_id);
    bool notify_event(uint16_t service_id, uint16_t event_id, const uint8_t* payload, size_t len);

    // Consumer
    bool find_service(uint16_t id, uint8_t major_version, uint32_t now_ms);
    std::optional<uint32_t> request(uint16_t service_id, uint16_t method_id,
                                     const uint8_t* payload, size_t len,
                                     RequestCallback cb, void* ctx,
                                     uint32_t timeout_ms, uint32_t now_ms);
    bool fire_and_forget(uint16_t service_id, uint16_t method_id,
                          const uint8_t* payload, size_t len);
    bool subscribe_event(uint16_t service_id, uint16_t event_id,
                          IEventHandler<Impl>& handler, uint16_t ttl, uint32_t now_ms);

    // Security
    bool set_hmac_key(const Address& peer, const uint8_t* key);

    // Diagnostics
    void set_diagnostic_callback(DiagnosticCallback cb, void* ctx);
    const DiagnosticCounters& diagnostics() const;
};
```

### IService

CRTP base for service implementations. Implement `impl_on_request()` and `impl_is_ready()`:

```cpp
template <typename Impl>
class IService {
    ReturnCode on_request(uint16_t method_id,
                          const uint8_t* payload, size_t payload_length,
                          uint8_t* response, size_t& response_length);
    bool is_ready() const;
};
```

### ITransport

CRTP base for platform-specific transport. Implement `impl_send()`, `impl_broadcast()`, and `impl_poll()`:

```cpp
template <typename Impl, typename Config>
class ITransport {
    bool send(const Address& destination, const uint8_t* data, size_t length);
    bool broadcast(const uint8_t* data, size_t length);
    bool poll(Address& source, const uint8_t*& data, size_t& length);
};
```

### IEventHandler

CRTP base for receiving event notifications. Implement `impl_on_event()`:

```cpp
template <typename Impl>
class IEventHandler {
    void on_event(uint16_t service_id, uint16_t event_id,
                  const uint8_t* payload, size_t payload_length);
};
```

---

## Security

Sero provides two layers of message integrity and authentication:

### CRC-16

Every message includes a 2-byte CRC-16 trailer computed over the entire message (header + payload + optional HMAC). Messages failing CRC validation are silently discarded and counted.

### HMAC-SHA256-128

Optional per-peer message authentication using pre-shared 256-bit keys, producing a 128-bit (truncated) HMAC appended before the CRC trailer. Enable by setting the `AUTH` flag or requiring authentication on a service:

```cpp
// Configure a pre-shared key for a peer
rt.set_hmac_key(peer_address, key_bytes);

// Register a service that requires authentication
rt.register_service(service_id, svc, major, minor, /*auth_required=*/true);
```

### Sequence Counter

Per-device monotonic counter with a configurable acceptance window (`SeqCounterAcceptWindow`) to detect duplicate and stale messages.

---

## Diagnostics

Sero tracks 9 diagnostic counters that can be read at any time:

| Counter | Trigger |
|---|---|
| `CrcErrors` | CRC-16 mismatch |
| `VersionMismatches` | Unknown protocol version |
| `OversizedPayloads` | Payload exceeds `MaxPayloadSize` |
| `TypeIdMismatches` | Method/Event ID inconsistent with message type |
| `DuplicateMessages` | Sequence counter duplicate |
| `StaleMessages` | Sequence counter outside acceptance window |
| `AuthFailures` | HMAC verification failed |
| `UnknownMessageTypes` | Unrecognized message type |
| `DroppedMessages` | Queue overflow, send failure, unroutable message, etc. |

Register an optional callback to be notified on each discard:

```cpp
rt.set_diagnostic_callback(
    [](DiagnosticCounter counter, const uint8_t* header, void* ctx) {
        // Log or report the discarded message
    },
    user_context
);
```

---

## Examples

Complete working examples are provided in the [examples/](examples/) directory:

| File | Description |
|---|---|
| [examples/server.cpp](examples/server.cpp) | Calculator service: `Add`, `Multiply` methods + `Counter` event |
| [examples/client.cpp](examples/client.cpp) | Discovers the calculator, subscribes to events, calls methods |
| [examples/udp_transport.hpp](examples/udp_transport.hpp) | UDP/multicast transport implementation for Linux |

### Running the Examples

```bash
# Build
cmake -B build
cmake --build build

# Terminal 1: Start the server
./build/example_server

# Terminal 2: Start the client
./build/example_client
```

The client will discover the server via multicast service discovery, subscribe to the `Counter` event, and alternate between `Add` and `Multiply` calls every 3 seconds.

---

## Testing

Sero includes a comprehensive test suite using [Google Test](https://github.com/google/googletest) (fetched automatically via CMake `FetchContent`).

```bash
# Build with tests (enabled by default)
cmake -B build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

### Test Coverage

The test suite covers all major components:

- Wire format serialization/deserialization
- CRC-16, SHA-256, HMAC computation and verification
- End-to-end protection (sequence counters, acceptance window)
- Message authentication
- Method dispatch and service registration
- Event manager (subscribe, notify, TTL eviction)
- Request tracking and timeout eviction
- Service discovery state machines and SD payloads
- Full runtime integration tests

### Code Coverage Report

```bash
cmake -B build -DCODE_COVERAGE=ON
cmake --build build
cd build && make coverage
# Open build/coverage/index.html
```

---

## Protocol Specification

The full protocol specification is available in [idea.md](idea.md). Key aspects:

### Wire Format

```
[Header (20 B)] [Payload (N B)] [CRC-16 (2 B)]
```

With authentication:

```
[Header (20 B)] [Payload (N B)] [HMAC-128 (16 B)] [CRC-16 (2 B)]
```

### Message Types

| Value | Name | Description |
|---|---|---|
| `0x00` | `REQUEST` | Request expecting a response |
| `0x01` | `REQUEST_NO_RETURN` | Fire-and-forget |
| `0x02` | `RESPONSE` | Response to a request |
| `0x03` | `NOTIFICATION` | Event pushed to subscribers |
| `0x80` | `ERROR` | Error response |

### Service Discovery Operations

| Method ID | Name | Direction |
|---|---|---|
| `0x0001` | `SD_OFFER_SERVICE` | Provider → Network (broadcast) |
| `0x0002` | `SD_FIND_SERVICE` | Consumer → Network (broadcast) |
| `0x0003` | `SD_SUBSCRIBE_EVENT` | Consumer → Provider (unicast) |
| `0x0004` | `SD_SUBSCRIBE_ACK` | Provider → Consumer (unicast) |
| `0x0005` | `SD_UNSUBSCRIBE` | Consumer → Provider (unicast) |

---

## Project Structure

```
sero/
├── CMakeLists.txt              # Build system
├── include/
│   ├── sero.hpp           # Umbrella header
│   └── sero/
│       ├── runtime.hpp         # Top-level Runtime coordinator
│       ├── core/
│       │   ├── config.hpp      # Compile-time configuration
│       │   ├── types.hpp       # Enums, constants, type aliases
│       │   ├── message_header.hpp  # 20-byte header serialize/deserialize
│       │   ├── transport.hpp   # ITransport CRTP base
│       │   └── diagnostic_counters.hpp
│       ├── security/
│       │   ├── crc16.hpp       # CRC-16 implementation
│       │   ├── sha256.hpp      # SHA-256 implementation
│       │   ├── hmac.hpp        # HMAC-SHA256 implementation
│       │   ├── e2e_protection.hpp  # CRC + sequence counter
│       │   └── message_authenticator.hpp  # Per-peer HMAC management
│       └── service/
│           ├── service.hpp     # IService CRTP base
│           ├── event_handler.hpp   # IEventHandler CRTP base
│           ├── method_dispatcher.hpp
│           ├── event_manager.hpp
│           ├── request_tracker.hpp
│           └── service_discovery.hpp
├── examples/
│   ├── server.cpp              # Calculator service example
│   ├── client.cpp              # Client example
│   └── udp_transport.hpp       # UDP transport for examples
├── test/
│   ├── CMakeLists.txt
│   ├── test_runtime.cpp        # Integration tests
│   ├── test_crc16.cpp
│   ├── test_sha256.cpp
│   ├── test_hmac.cpp
│   └── ...                     # Component-level tests
└── idea.md                     # Full protocol specification
```

---

## Roadmap

The following features are planned but not yet implemented:

- [ ] Payload segmentation / reassembly
- [ ] Event groups
- [ ] SOME/IP-style fields (getter/setter/notifier)
- [ ] QoS / priority / deadline monitoring
- [ ] Graceful shutdown (`SD_STOP_OFFER`)
- [ ] Dynamic HMAC key exchange / rotation
- [ ] Asymmetric authentication
- [ ] Subscription eviction notification
- [ ] Protocol version negotiation
- [ ] CMake install target with `seroConfig.cmake`

---

## Contributing

Contributions are welcome! Please follow these steps:

1. **Fork** the repository
2. **Create a branch** for your feature or fix (`git checkout -b feature/my-feature`)
3. **Write tests** for any new functionality
4. **Ensure all tests pass** (`cd build && ctest --output-on-failure`)
5. **Commit** with clear, descriptive messages
6. **Open a Pull Request** against `main`

### Guidelines

- Follow the existing code style (C++17, no exceptions, no RTTI, no heap allocation)
- All public APIs must be documented with `///` doc comments
- New features should include corresponding unit tests
- Keep the header-only design — no `.cpp` files in the library

---

## License

This project is licensed under the [MIT License](LICENSE).

---

**Sero** — Deterministic service-oriented communication for embedded systems.
