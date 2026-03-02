# Sero Protocol Specification

**Version:** 0.1.0-draft
**Date:** 2026-02-28
**Status:** Draft

---

## 1. Overview

Sero is an embedded-first, realtime-ready communication protocol for service-oriented inter-device communication. It provides service discovery, remote method invocation, and event notification for resource-constrained MCUs and safety-critical environments.

The protocol defines message framing, addressing, and state machines. Transport and payload serialization are opaque — the application provides both.

### 1.1 Design Principles

- **Zero heap allocation.** All buffers and tables are compile-time sized.
- **No mutexes.** Run-to-completion execution; no priority inversion.
- **No exceptions, no RTTI.** Defensive checks only.
- **Bounded time and space.** Every operation has a compile-time upper bound.
- **CRTP interfaces.** Zero-overhead polymorphism; users provide concrete implementations at compile time.
- **C++17.** `constexpr if`, fold expressions, structured bindings, `std::optional`.

### 1.2 Terminology

| Term | Definition |
|---|---|
| **Device** | A network node running a Sero stack. MAY act as both provider and consumer. |
| **Service** | A logical grouping of methods and events, identified by a 16-bit Service ID. |
| **Method** | A callable function on a service (ID range `0x0000`–`0x7FFF`). |
| **Event** | A notification from a service (ID range `0x8000`–`0xFFFF`, bit 15 = 1). |
| **Client ID** | 16-bit device identifier, statically assigned, network-unique. `0x0000` is reserved (used in provider-originated notifications). |
| **Request ID** | 32-bit caller-generated correlation ID. Monotonically incrementing, wrapping. MUST be unique among pending requests on the sender. |
| **Transport** | User-implemented layer for sending/receiving raw byte buffers. |

---

## 2. Wire Format

All multi-byte fields are **big-endian**. All reserved bytes MUST be `0x00` on send and MUST be ignored on receive.

### 2.1 Message Structure

```
[Header (20 B)] [Payload (N B)] [CRC-16 (2 B)]
```

With authentication (`AUTH` flag set):

```
[Header (20 B)] [Payload (N B)] [HMAC-128 (16 B)] [CRC-16 (2 B)]
```

**Minimum message size:** 22 bytes (empty payload, no auth).
**Maximum message size:** 20 + `MaxPayloadSize` + (16 if AUTH) + 2.

### 2.2 Header Layout (20 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Version    |  Message Type |  Return Code  |     Flags     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Service ID           |        Method/Event ID        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Client ID           | Sequence Cnt  |   Reserved    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Request ID                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Payload Length                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | **Version** | `0x01` for this specification. |
| 1 | 1 | **Message Type** | See §2.3. |
| 2 | 1 | **Return Code** | See §2.4. MUST be `0x00` in requests. Exception: `SD_SUBSCRIBE_ACK` carries the subscription result here. |
| 3 | 1 | **Flags** | Bit 0 = `AUTH` (HMAC trailer present). Bits 1–7 reserved, MUST be 0. |
| 4 | 2 | **Service ID** | Target service. `0xFFFF` = Service Discovery. |
| 6 | 2 | **Method/Event ID** | Bit 15 = type discriminator: `0` = method, `1` = event. |
| 8 | 2 | **Client ID** | Sender's Client ID in requests/SD. Echoed in responses. `0x0000` in notifications. |
| 10 | 1 | **Sequence Counter** | Per-device monotonic counter, wraps at `0xFF`. |
| 11 | 1 | **Reserved** | `0x00`. |
| 12 | 4 | **Request ID** | Correlation ID. Echoed in responses. `0x00000000` in notifications. |
| 16 | 4 | **Payload Length** | Byte count of payload only (excludes HMAC and CRC trailers). MUST NOT exceed `MaxPayloadSize`. |

### 2.3 Message Types

| Value | Name | Description |
|-------|------|-------------|
| `0x00` | `REQUEST` | Request expecting a response. |
| `0x01` | `REQUEST_NO_RETURN` | Fire-and-forget. No response. MUST NOT be used for ASIL-rated functions (see §5.2). |
| `0x02` | `RESPONSE` | Successful response to `REQUEST`. |
| `0x03` | `NOTIFICATION` | Event pushed to subscribers. |
| `0x80` | `ERROR` | Error response to `REQUEST`. Bit 7 = error indicator. |

Values `0x04`–`0x7F` and `0x81`–`0xFF` are reserved. Messages with reserved types MUST be discarded (→ `unknown_message_types`).

### 2.4 Return Codes

| Value | Name | Wire? | Description |
|-------|------|-------|-------------|
| `0x00` | `E_OK` | Yes | Success. |
| `0x01` | `E_NOT_OK` | Yes | Unspecified error. |
| `0x02` | `E_UNKNOWN_SERVICE` | Yes | Service ID not available. |
| `0x03` | `E_UNKNOWN_METHOD` | Yes | Method/Event ID not available on service. |
| `0x04` | `E_NOT_READY` | Yes | Service exists but not ready. |
| `0x05` | `E_NOT_REACHABLE` | Yes | Target device unreachable. |
| `0x06` | `E_TIMEOUT` | **No** | Local-only: request timed out. Never sent on wire. |
| `0x07` | `E_MALFORMED_MESSAGE` | Yes | Invalid header, payload too large, etc. |
| `0x08` | `E_AUTH_FAILED` | **No** | Local-only: HMAC verification failed. Never sent on wire. |
| `0x09` | `E_DUPLICATE` | **No** | Local-only: duplicate detected via sequence counter. |
| `0x0A`–`0x3F` | — | — | Reserved (protocol). |
| `0x40`–`0x5F` | — | — | Reserved (security). |
| `0x60`–`0xFF` | — | — | User/application-defined. |

### 2.5 Method/Event ID Consistency

Receivers MUST verify bit 15 of Method/Event ID matches the Message Type:

| Message Type | Required bit 15 |
|---|---|
| `REQUEST`, `REQUEST_NO_RETURN`, `RESPONSE`, `ERROR` | `0` |
| `NOTIFICATION` | `1` |

Mismatch → discard (→ `type_id_mismatches`).

### 2.6 Client ID Validation

Receivers MUST discard any `REQUEST` or `REQUEST_NO_RETURN` message where Client ID = `0x0000` (→ `dropped_messages`). Client ID `0x0000` is valid only in `NOTIFICATION` messages.

---

## 3. Transport Abstraction

### 3.1 Address Type

```cpp
using Address = std::array<uint8_t, TransportAddressSize>;
```

Format is transport-specific. Unused trailing bytes MUST be zero-padded. All devices MUST use the same `TransportAddressSize`.

### 3.2 Interface (Pull Model)

The transport enqueues received frames; `Runtime::process()` drains via `poll()`.

```cpp
template <typename Impl>
class ITransport {
public:
    // Unicast send. Returns true on success, false on failure.
    bool send(Address destination, const uint8_t* data, size_t length);

    // Broadcast send (for SD messages). Returns true on success.
    bool broadcast(const uint8_t* data, size_t length);

    // Dequeue next received message. Returns false if queue empty.
    // Returned data pointer valid only until next poll() call.
    bool poll(Address& source, const uint8_t*& data, size_t& length);
};
```

All methods delegate to `static_cast<Impl*>(this)->method(...)` (CRTP).

### 3.3 Behavioral Contract

| Requirement | Rule |
|---|---|
| **Receive buffer** | Fixed-size ring buffer, capacity = `MaxReceiveQueueSize`. |
| **Buffer full policy** | MUST drop the **newest** incoming frame and increment `dropped_messages`. |
| **`poll()` data lifetime** | Valid until next `poll()` call. Runtime MUST fully process before next `poll()`. |
| **`send()`/`broadcast()` failure** | On `false` return, the runtime increments `dropped_messages`. No automatic retry. |
| **Framing** | Transport MUST deliver complete Sero messages (header + payload + trailers). |
| **Completeness** | Transport delivers frames; protocol verifies integrity independently (CRC-16, sequence counter, HMAC). |

---

## 4. Service Discovery (SD)

### 4.1 SD Header Fields

All SD messages use:
- **Service ID** = `0xFFFF`
- **Message Type** = `REQUEST_NO_RETURN` (`0x01`)
- **Client ID** = sender's device Client ID
- **Method/Event ID** = SD operation (bit 15 = 0, per §2.5)

### 4.2 SD Operations

| Method ID | Name | Direction | Transport |
|-----------|------|-----------|-----------|
| `0x0001` | `SD_OFFER_SERVICE` | Provider → Network | Broadcast (periodic) or Unicast (in response to Find) |
| `0x0002` | `SD_FIND_SERVICE` | Consumer → Network | Broadcast |
| `0x0003` | `SD_SUBSCRIBE_EVENT` | Consumer → Provider | Unicast |
| `0x0004` | `SD_SUBSCRIBE_ACK` | Provider → Consumer | Unicast |
| `0x0005` | `SD_UNSUBSCRIBE` | Consumer → Provider | Unicast |

### 4.3 SD Payloads

#### SD_OFFER_SERVICE (8 + `TransportAddressSize` bytes)

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Offered Service ID      |  Major Version| Minor Version |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           TTL (seconds)       |         Reserved (0x0000)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Provider Address (TransportAddressSize bytes)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **TTL**: MUST be ≥ 1. Value `0x0000` is forbidden (receivers MUST ignore offers with TTL = 0).
- **Version**: Consumer MUST ignore offers where Major Version differs from its expected version for that Service ID.

#### SD_FIND_SERVICE (4 bytes)

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Requested Service ID    |         Reserved (0x0000)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- `0xFFFF` = wildcard. Provider MUST reply with one unicast `SD_OFFER_SERVICE` per hosted service.

#### SD_SUBSCRIBE_EVENT / SD_SUBSCRIBE_ACK / SD_UNSUBSCRIBE (8 bytes)

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Target Service ID       |         Target Event ID       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        TTL (seconds)          |         Reserved (0x0000)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **Target Event ID**: MUST have bit 15 = 1.
- **SUBSCRIBE TTL**: `0x0000` = use provider's `SubscriptionTtlSeconds` default. Otherwise MUST be ≥ 1.
- **ACK**: Return Code `E_OK` = accepted (TTL field = granted TTL). `E_NOT_OK` = rejected (TTL field = `0x0000`).
- **ACK granted TTL**: The consumer MUST use the granted TTL (not its requested TTL) for renewal timing.
- **UNSUBSCRIBE**: TTL field MUST be `0x0000`.

### 4.4 SD Timing

| Behavior | Rule |
|---|---|
| **Startup** | Broadcast `SD_OFFER_SERVICE` for each hosted service. |
| **Re-announce** | MUST re-broadcast offers at interval = `TTL / 2`. |
| **On-demand reply** | On `SD_FIND_SERVICE` match, unicast `SD_OFFER_SERVICE` to requester. |
| **Find retry** | Exponential backoff: `delay_n = SdFindInitialDelayMs * SdFindBackoffMultiplier^n + random(-SdFindJitterMs, +SdFindJitterMs)`, clamped to minimum 0. After `SdFindRetryCount` retries → `NOT_FOUND`, notify app. |
| **Offer expiry** | Consumer transitions service to `NOT_FOUND` if TTL expires without re-offer. |
| **Subscription renewal** | Consumer MUST re-send `SD_SUBSCRIBE_EVENT` at interval = granted `TTL / 2`. |

### 4.5 SD State Machine — Provider (per service)

```
        init()──▶ [NOT_OFFERED]
                       │ offer()
                       ▼
                  [OFFERED] ──── FindService rx ────▶ unicast Offer
                  [OFFERED] ──── TTL/2 elapsed ─────▶ re-broadcast Offer
                       │ stop_offer()
                       ▼
                  [NOT_OFFERED]
```

### 4.6 SD State Machine — Consumer (per service)

```
        init()──▶ [NOT_FOUND]
                       │ find()
                       ▼
                  [SEARCHING] ── retries exhausted ──▶ [NOT_FOUND] (notify app)
                  [SEARCHING] ── timeout ────────────▶ re-send (backoff)
                       │ Offer rx (matching major version)
                       ▼
                  [FOUND] ── TTL expired ────────────▶ [NOT_FOUND] (notify app)
                  [FOUND] ── re-offer rx ────────────▶ reset TTL
```

**Multiple providers:** The consumer tracks the most recently received valid offer per Service ID. Provider change (different address) updates the tracked entry and invokes the application callback if registered.

---

## 5. Method Invocation

### 5.1 Request/Response

1. Client sends `REQUEST` via unicast to provider address (learned via SD).
2. Provider dispatches:
   - Service ID not registered → `ERROR` with `E_UNKNOWN_SERVICE`.
   - Method ID not registered → `ERROR` with `E_UNKNOWN_METHOD`.
   - Service not ready → `ERROR` with `E_NOT_READY`.
   - Otherwise → invoke handler, send `RESPONSE` or `ERROR`.
3. Response MUST echo Client ID, Request ID, Service ID, and Method ID.
4. Client correlates response by Request ID.
5. If the pending request table is full when the client initiates a new request, the call MUST fail locally with `E_NOT_OK`. No message is sent.

**Timeout:** If no response arrives within the per-request timeout (default: `RequestTimeoutMs`), the request is evicted and the application receives a local `E_TIMEOUT` callback. No message is sent on the wire.

### 5.2 Fire-and-Forget (`REQUEST_NO_RETURN`)

**MUST NOT** be used for ASIL-rated functions. No delivery confirmation, no error feedback.

1. Client sends `REQUEST_NO_RETURN` via unicast.
2. Provider invokes handler if service/method registered; otherwise silently discards (→ `dropped_messages`).
3. No response is sent under any circumstance.

E2E protection (sequence counter, CRC-16) still applies.

---

## 6. Event Notification

### 6.1 Subscription Flow

```
Consumer                          Provider
   │── SD_SUBSCRIBE_EVENT ──────────▶│
   │◀─────────── SD_SUBSCRIBE_ACK ──│  (E_OK + granted TTL)
   │◀─────────── NOTIFICATION ──────│
   │── SD_SUBSCRIBE_EVENT ──────────▶│  (renewal at TTL/2)
   │◀─────────── SD_SUBSCRIBE_ACK ──│
   │── SD_UNSUBSCRIBE ─────────────▶│
```

- Expired subscriptions are silently evicted. No notification is sent.
- On `SD_SUBSCRIBE_ACK` with `E_NOT_OK`, the consumer MUST NOT expect notifications and MUST NOT retry automatically. The application is notified and decides whether to retry.

### 6.2 Notification Message Fields

| Field | Value |
|---|---|
| Message Type | `NOTIFICATION` (`0x03`) |
| Client ID | `0x0000` |
| Request ID | `0x00000000` |
| Method/Event ID | Event ID (bit 15 = 1) |

Sent via unicast to each subscribed consumer.

### 6.3 Subscription Tracking (Provider)

- Fixed-size table per event: `MaxSubscribers` entries.
- Each entry: Client ID + transport address + TTL expiry timestamp.
- Table full → `SD_SUBSCRIBE_ACK` with `E_NOT_OK`.
- Renewal from existing subscriber resets TTL (no new slot consumed).
- Expired entries evicted on each `Runtime::process()` cycle.

---

## 7. End-to-End Protection

### 7.1 CRC-16

**Algorithm:** CRC-16/CCITT-FALSE — polynomial `0x1021`, init `0xFFFF`, no reflection, final XOR `0x0000`.

**Scope:** Computed over header + payload + HMAC (if present). The 2-byte CRC is appended last.

### 7.2 Sequence Counter

Each device maintains a single 8-bit outgoing counter, incremented per message sent, wrapping `0xFF` → `0x00`.

Receivers track last-seen counter **per source transport address** (not Client ID), bounded by `MaxTrackedPeers`.

**Validation** (let `delta = (received - last_seen) mod 256`):

| Condition | Action |
|---|---|
| `delta == 0` | **Duplicate.** Discard. → `duplicate_messages`. |
| `1 ≤ delta ≤ SeqCounterAcceptWindow` | **Accept.** Update last-seen. |
| `delta > SeqCounterAcceptWindow` | **Stale.** Discard. → `stale_messages`. |

**First-seen peer:** Accept unconditionally; store as initial last-seen.

**Tracking table full:** Accept without sequence validation. → `dropped_messages`.

### 7.3 HMAC Authentication (Optional)

When `AUTH` flag (bit 0 of Flags) is set:

- **Algorithm:** HMAC-SHA256, truncated to 128 bits (16 bytes).
- **Scope:** Computed over header (20 B, with AUTH flag set) + payload.
- **Key:** Pre-shared symmetric key per device pair (`HmacKeySize` bytes). Key management is outside this spec.

**Per-service policy** (compile-time):

| Policy | Behavior |
|---|---|
| Auth required | Discard messages without `AUTH` flag → `auth_failures`. Verify HMAC; discard on failure → `auth_failures`. |
| Auth not required | Ignore `AUTH` flag. If HMAC bytes are present, include them in CRC computation but do not verify. |

### 7.4 Receive Validation Order

Every received message MUST be validated in this exact order. Each failure discards the message and increments the indicated counter.

| Step | Check | Counter |
|------|-------|---------|
| 1 | Message size ≥ 22 bytes | `dropped_messages` |
| 2 | CRC-16 (over all bytes except final 2) | `crc_errors` |
| 3 | Version = `0x01` | `version_mismatches` |
| 4 | Payload Length ≤ `MaxPayloadSize` | `oversized_payloads` |
| 5 | Message Type is known (`0x00`–`0x03`, `0x80`) | `unknown_message_types` |
| 6 | Method/Event ID bit 15 consistent with Message Type | `type_id_mismatches` |
| 7 | Client ID valid for message type (§2.6) | `dropped_messages` |
| 8 | Sequence counter | `duplicate_messages` or `stale_messages` |
| 9 | HMAC (if `AUTH` flag set or service requires auth) | `auth_failures` |
| 10 | Dispatch to handler | — |

The diagnostic callback (if registered) is invoked on every discard with the counter type and the raw header bytes (if available).

---

## 8. Compile-Time Configuration

All resource limits are `constexpr` or template parameters. No dynamic allocation.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `MaxPayloadSize` | `size_t` | `1400` | Max payload bytes per message. |
| `MaxServices` | `size_t` | `16` | Max hosted services per device. |
| `MaxMethods` | `size_t` | `32` | Max methods per service. |
| `MaxEvents` | `size_t` | `16` | Max events per service. |
| `MaxSubscribers` | `size_t` | `8` | Max subscribers per event. |
| `MaxPendingRequests` | `size_t` | `16` | Max in-flight client requests. |
| `MaxKnownServices` | `size_t` | `32` | Max remote services tracked (consumer SD). |
| `RequestTimeoutMs` | `uint32_t` | `1000` | Default request timeout (ms). |
| `OfferTtlSeconds` | `uint16_t` | `5` | Default service offer TTL. |
| `SubscriptionTtlSeconds` | `uint16_t` | `10` | Default event subscription TTL. |
| `SdFindRetryCount` | `uint8_t` | `3` | SD_FIND_SERVICE max retries. |
| `SdFindInitialDelayMs` | `uint32_t` | `100` | Initial retry delay (ms). |
| `SdFindBackoffMultiplier` | `uint8_t` | `2` | Retry delay multiplier. |
| `SdFindJitterMs` | `uint32_t` | `50` | Max ± jitter on retry delay (ms). |
| `SeqCounterAcceptWindow` | `uint8_t` | `15` | Sequence counter acceptance window. |
| `TransportAddressSize` | `size_t` | `8` | Fixed transport address size (bytes). |
| `MaxReceiveQueueSize` | `size_t` | `16` | Transport receive ring buffer capacity. |
| `MaxTrackedPeers` | `size_t` | `32` | Max peers tracked for sequence validation. |
| `HmacKeySize` | `size_t` | `32` | Pre-shared HMAC key size (bytes). |
| `MaxDtcs` | `size_t` | `32` | Max stored Diagnostic Trouble Codes per device. 0 disables DTC storage. |

---

## 9. Diagnostics

### 9.1 Counters

`uint32_t` values, wrapping on overflow, readable at any time.

| Counter | Trigger |
|---------|---------|
| `crc_errors` | CRC-16 mismatch. |
| `version_mismatches` | Unknown protocol version. |
| `oversized_payloads` | Payload Length > `MaxPayloadSize`. |
| `type_id_mismatches` | Method/Event ID bit 15 inconsistent with Message Type. |
| `duplicate_messages` | Sequence counter duplicate. |
| `stale_messages` | Sequence counter outside acceptance window. |
| `auth_failures` | HMAC failure or missing auth on auth-required service. |
| `unknown_message_types` | Unrecognized Message Type. |
| `dropped_messages` | Catch-all: queue overflow, tracking table full, unroutable fire-and-forget, send failure, invalid Client ID. |

### 9.2 Callback

Application MAY register a diagnostic callback invoked on each discard. Parameters: counter type + raw header bytes (20 B, or `nullptr` if message was too short to contain a header).

---

## 10. Diagnostics Service (OBD-II Style Remote Monitoring)

### 10.1 Overview

Sero provides an optional built-in **Diagnostics Service** (Service ID `0xFFFE`) that lets
remote peers query a device's health — similar to connecting an OBD-II scanner to a car's
ECU. The service is opt-in: call `Runtime::enable_diagnostics(now_ms)` to register and
auto-offer it via Service Discovery.

Design goals:
- **Lightweight**: fixed-size DTC table (`MaxDtcs`, default 32), no heap, no background threads.
- **Non-invasive**: all queries are poll-based requests; the device does no extra work between queries.
- **Discoverable**: a desktop scanner calls `find_service(0xFFFE, ...)` to discover all
  diagnostics-enabled devices on the network.

### 10.2 DTC Model

A **Diagnostic Trouble Code (DTC)** records an application-defined error on the device.

| Field | Type | Description |
|-------|------|-------------|
| `code` | `uint16_t` | Application-defined error code. |
| `severity` | `DtcSeverity` | `Info(0)`, `Warning(1)`, `Error(2)`, `Fatal(3)`. |
| `status` | `uint8_t` | `1` = active, `0` = cleared. |
| `occurrence_count` | `uint32_t` | How many times this code was reported. |
| `first_seen_ms` | `uint32_t` | Uptime (ms) when first reported. |
| `last_seen_ms` | `uint32_t` | Uptime (ms) of most recent report. |

DTCs are stored in a fixed-size table of `MaxDtcs` slots. If the table is full, new codes
are silently dropped (→ `dropped_messages` counter). Re-reporting an existing code
increments `occurrence_count` and updates `last_seen_ms`.

Application API:
```cpp
rt.report_dtc(0x0010, DtcSeverity::Error, now_ms);
rt.clear_dtc(0x0010);
rt.clear_all_dtcs();
```

### 10.3 Diagnostics Method IDs

All methods are under **Service ID `0xFFFE`** (reserved, like `0xFFFF` for SD).

| Method | ID | Request Payload | Response Payload |
|--------|----|-----------------|------------------|
| `DIAG_GET_DTCS` | `0x0001` | (empty) | `[2B count][N × 16B DTC entries]` |
| `DIAG_CLEAR_DTCS` | `0x0002` | `[2B code]` (`0xFFFF` = clear all) | (empty) |
| `DIAG_GET_COUNTERS` | `0x0003` | (empty) | `[9 × 4B counters]` = 36 bytes |
| `DIAG_GET_SERVICE_LIST` | `0x0004` | (empty) | `[2B count][N × 6B service entries]` |
| `DIAG_GET_DEVICE_INFO` | `0x0005` | (empty) | `[2B client_id][4B uptime_ms][1B version][1B reserved]` = 8 bytes |

### 10.4 Wire Payload Formats (Big-Endian)

**DTC entry (16 bytes):**
```
[code (2B)] [severity (1B)] [status (1B)] [occ_count (4B)] [first_seen (4B)] [last_seen (4B)]
```

**Service list entry (6 bytes):**
```
[service_id (2B)] [major (1B)] [minor (1B)] [auth_required (1B)] [ready (1B)]
```

**Counter response:**  9 × `uint32_t` in `DiagnosticCounter` enum order
(`CrcErrors, VersionMismatches, ..., DroppedMessages`).

### 10.5 Severity Enum

```cpp
enum class DtcSeverity : uint8_t {
    Info    = 0,
    Warning = 1,
    Error   = 2,
    Fatal   = 3,
};
```

### 10.6 Scanner Interaction

1. Scanner calls `find_service(0xFFFE, 1, now)` → SD discovers all devices offering the diagnostics service.
2. On `on_service_found` callback, scanner issues `DIAG_GET_DEVICE_INFO` + `DIAG_GET_SERVICE_LIST` + `DIAG_GET_DTCS` + `DIAG_GET_COUNTERS` requests.
3. Responses are parsed and displayed in a dashboard.
4. To clear DTCs: scanner sends `DIAG_CLEAR_DTCS` with the target code (or `0xFFFF` for all).

---

## 11. Implementation Architecture (C++17)

### 11.1 CRTP Interfaces

```cpp
template <typename Impl>
class ITransport {
public:
    bool send(Address dest, const uint8_t* data, size_t len);
    bool broadcast(const uint8_t* data, size_t len);
    bool poll(Address& src, const uint8_t*& data, size_t& len);
};

template <typename Impl>
class IService {
public:
    // Dispatched for REQUEST and REQUEST_NO_RETURN.
    // For REQUEST_NO_RETURN the return code and response buffer are ignored.
    ReturnCode on_request(uint16_t method_id,
                          const uint8_t* payload, size_t payload_length,
                          uint8_t* response, size_t& response_length);
    bool is_ready() const;
};

template <typename Impl>
class IEventHandler {
public:
    void on_event(uint16_t service_id, uint16_t event_id,
                  const uint8_t* payload, size_t payload_length);
};
```

### 11.2 Core Components

| Component | Responsibility |
|-----------|---------------|
| `MessageHeader` | 20-byte header serialize/deserialize. |
| `E2EProtection` | CRC-16 + sequence counter. |
| `MessageAuthenticator` | HMAC-128 compute/verify. |
| `ServiceDiscovery` | SD state machines, TTL management, retry logic. |
| `MethodDispatcher` | Route requests to service handlers. |
| `EventManager` | Subscriber tables, TTL eviction, notification fan-out. |
| `RequestTracker` | Pending request table, timeout eviction. |
| `DiagnosticCounters` | Counter storage + application callback dispatch. |
| `Runtime` | Top-level coordinator (see §10.3). |

### 11.3 `Runtime::process()` Cycle

Called by user (main loop or RTOS task). Run-to-completion, non-blocking.

1. Drain transport via `poll()`, for each message:
   a. Execute validation pipeline (§7.4).
   b. Dispatch to handler by Message Type.
2. Evict timed-out pending requests (→ local `E_TIMEOUT` callbacks).
3. Evict expired subscriptions (provider side).
4. Re-broadcast offers at TTL/2.
5. Re-send subscription renewals at granted-TTL/2.
6. Transition consumer SD entries whose TTL expired → `NOT_FOUND`.

---

## 12. Future Extensions (Out of Scope)

- Payload segmentation / reassembly
- Event groups
- SOME/IP-style fields (getter/setter/notifier)
- QoS / priority / deadline monitoring
- Graceful shutdown (`SD_STOP_OFFER`)
- Dynamic HMAC key exchange / rotation
- Asymmetric authentication
- Subscription eviction notification
- Protocol version negotiation