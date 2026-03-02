#pragma once
/// @file udp_transport.hpp
/// UDP transport implementation for Sero examples.
/// Address encoding (8 bytes): [IP4 (4B)] [port BE (2B)] [padding (2B)]

#include <sero.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace example {

using Config = sero::DefaultConfig;
using Addr   = sero::Address<Config>;

// ── Address helpers ─────────────────────────────────────────────

inline Addr make_addr(const char* ip, uint16_t port) {
    Addr a{};
    inet_pton(AF_INET, ip, a.data());
    a[4] = static_cast<uint8_t>(port >> 8);
    a[5] = static_cast<uint8_t>(port & 0xFF);
    return a;
}

inline sockaddr_in addr_to_sockaddr(const Addr& a) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    std::memcpy(&sa.sin_addr, a.data(), 4);
    sa.sin_port = htons(static_cast<uint16_t>((a[4] << 8) | a[5]));
    return sa;
}

inline Addr sockaddr_to_addr(const sockaddr_in& sa) {
    Addr a{};
    std::memcpy(a.data(), &sa.sin_addr, 4);
    uint16_t port = ntohs(sa.sin_port);
    a[4] = static_cast<uint8_t>(port >> 8);
    a[5] = static_cast<uint8_t>(port & 0xFF);
    return a;
}

// ── Multicast group for SD broadcast ────────────────────────────

static constexpr const char* MCAST_GROUP = "239.0.0.1";
static constexpr uint16_t    MCAST_PORT  = 30490;

// ── UDP Transport ───────────────────────────────────────────────

class UdpTransport : public sero::ITransport<UdpTransport, Config> {
public:
    static constexpr std::size_t MAX_BUF = sero::HEADER_SIZE +
        Config::MaxPayloadSize + sero::HMAC_SIZE + sero::CRC_SIZE;

    UdpTransport() = default;
    ~UdpTransport() { shutdown(); }

    /// Initialize: bind unicast socket + join multicast group.
    bool init(const char* bind_ip, uint16_t bind_port) {
        // ── Unicast socket ──────────────────────────────────────
        unicast_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (unicast_fd_ < 0) { perror("socket(unicast)"); return false; }

        int reuse = 1;
        setsockopt(unicast_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        set_nonblocking(unicast_fd_);

        sockaddr_in uaddr{};
        uaddr.sin_family = AF_INET;
        uaddr.sin_port   = htons(bind_port);
        inet_pton(AF_INET, bind_ip, &uaddr.sin_addr);
        if (::bind(unicast_fd_, reinterpret_cast<sockaddr*>(&uaddr), sizeof(uaddr)) < 0) {
            perror("bind(unicast)"); return false;
        }

        // ── Multicast socket ────────────────────────────────────
        mcast_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (mcast_fd_ < 0) { perror("socket(mcast)"); return false; }

        setsockopt(mcast_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(mcast_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
        set_nonblocking(mcast_fd_);

        sockaddr_in maddr{};
        maddr.sin_family      = AF_INET;
        maddr.sin_port        = htons(MCAST_PORT);
        maddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(mcast_fd_, reinterpret_cast<sockaddr*>(&maddr), sizeof(maddr)) < 0) {
            perror("bind(mcast)"); return false;
        }

        // Join multicast group on the bind interface
        // (use bind_ip so multicast works over the real NIC,
        //  not just loopback — required for cross-device SD)
        ip_mreq mreq{};
        inet_pton(AF_INET, MCAST_GROUP, &mreq.imr_multiaddr);
        inet_pton(AF_INET, bind_ip, &mreq.imr_interface);
        if (setsockopt(mcast_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                       &mreq, sizeof(mreq)) < 0) {
            perror("IP_ADD_MEMBERSHIP"); return false;
        }

        // Set outgoing multicast interface to match bind address
        in_addr mcast_if{};
        inet_pton(AF_INET, bind_ip, &mcast_if);
        setsockopt(unicast_fd_, IPPROTO_IP, IP_MULTICAST_IF, &mcast_if, sizeof(mcast_if));

        // Enable multicast loopback (so we can test on one host)
        uint8_t loop = 1;
        setsockopt(unicast_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

        local_addr_ = make_addr(bind_ip, bind_port);
        return true;
    }

    void shutdown() {
        if (unicast_fd_ >= 0) { ::close(unicast_fd_); unicast_fd_ = -1; }
        if (mcast_fd_  >= 0) { ::close(mcast_fd_);  mcast_fd_  = -1; }
    }

    const Addr& local_addr() const { return local_addr_; }

    // ── CRTP implementation ─────────────────────────────────────

    bool impl_send(const Addr& dest, const uint8_t* data, std::size_t len) {
        sockaddr_in sa = addr_to_sockaddr(dest);
        ssize_t n = ::sendto(unicast_fd_, data, len, 0,
                             reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        return n == static_cast<ssize_t>(len);
    }

    bool impl_broadcast(const uint8_t* data, std::size_t len) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(MCAST_PORT);
        inet_pton(AF_INET, MCAST_GROUP, &sa.sin_addr);
        ssize_t n = ::sendto(unicast_fd_, data, len, 0,
                             reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
        return n == static_cast<ssize_t>(len);
    }

    bool impl_poll(Addr& source, const uint8_t*& data, std::size_t& len) {
        // Try multicast socket first, then unicast
        if (try_recv(mcast_fd_, source, data, len)) return true;
        if (try_recv(unicast_fd_, source, data, len)) return true;
        return false;
    }

private:
    int   unicast_fd_ = -1;
    int   mcast_fd_   = -1;
    Addr  local_addr_{};
    uint8_t recv_buf_[MAX_BUF]{};

    static void set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    bool try_recv(int fd, Addr& source, const uint8_t*& data, std::size_t& len) {
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        ssize_t n = ::recvfrom(fd, recv_buf_, sizeof(recv_buf_), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (n <= 0) return false;
        source = sockaddr_to_addr(from);
        data   = recv_buf_;
        len    = static_cast<std::size_t>(n);
        return true;
    }
};

// ── Monotonic clock helper ──────────────────────────────────────

inline uint32_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

} // namespace example
