//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: BSD-3-Clause
//
// This header must not be removed. The source files in this project may not be
// used to contribute to commercial projects or for monetary gain without the
// express written permission of the author.
//
// Cross-platform networking helpers: host interface enumeration (to prefill the
// cabinet-link settings), a non-blocking UDP socket for the link transport, and a
// non-blocking TCP listener for the cabinet outputs.
#pragma once

#include "core/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sm2::net {

/// One network interface as seen by the host, IPv4 only (the Model 2 comms ring
/// is IPv4).
struct Interface {
    std::string name;         ///< OS interface name, e.g. "en0" or "eth0".
    std::string ipv4;         ///< Dotted-quad address, e.g. "192.168.1.42".
    std::string subnet_mask;  ///< Dotted-quad mask, e.g. "255.255.255.0".
    bool        loopback = false;
};

/// Every up, address-carrying IPv4 interface; loopback is included but flagged.
[[nodiscard]] std::vector<Interface> interfaces();

/// First non-loopback IPv4 interface, or nullopt if only loopback exists.
[[nodiscard]] std::optional<Interface> primary_interface();

// -- non-blocking UDP socket ------------------------------------------------

/// A non-blocking datagram socket. Errors surface through valid()/last_error()
/// so a link that cannot bind degrades to "not linked" rather than throwing.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /// Bind to bind_ip:port; an empty bind_ip is INADDR_ANY. False on failure.
    bool open(const std::string& bind_ip, u16 port);

    void close();

    [[nodiscard]] bool valid() const { return m_fd != kInvalid; }

    /// Allow send_to() a broadcast address such as 255.255.255.255. False on
    /// failure.
    bool set_broadcast(bool enable);

    /// Send one datagram to dest_ip:port. False on error, including a full send
    /// buffer, which the caller treats as back-pressure.
    bool send_to(std::span<const u8> data, const std::string& dest_ip, u16 port);

    /// Receive one datagram into out. False when nothing is waiting or on error.
    bool recv_from(std::vector<u8>& out);

    [[nodiscard]] const std::string& last_error() const { return m_last_error; }

private:
#if defined(_WIN32)
    // Windows SOCKET is a pointer-sized unsigned handle.
    using Fd                       = std::uintptr_t;
    static constexpr Fd kInvalid   = static_cast<Fd>(~0ull);
#else
    using Fd                       = int;
    static constexpr Fd kInvalid   = -1;
#endif

    Fd          m_fd = kInvalid;
    std::string m_last_error;
};

// -- non-blocking TCP -------------------------------------------------------

#if defined(_WIN32)
using SocketFd                           = std::uintptr_t;
inline constexpr SocketFd kInvalidSocket = static_cast<SocketFd>(~0ull);
#else
using SocketFd                           = int;
inline constexpr SocketFd kInvalidSocket = -1;
#endif

/// One accepted TCP connection, non-blocking and with Nagle off, so a short
/// line goes out as soon as it is sent. Only what a line-oriented server needs:
/// sending, and noticing that the peer went away.
class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection();

    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    [[nodiscard]] bool valid() const { return m_fd != kInvalidSocket; }

    /// Send as much of `data` as the socket takes without blocking. The number of
    /// bytes taken, which is 0 when the peer is not reading and the buffer is
    /// full; nullopt once the connection has failed.
    [[nodiscard]] std::optional<usize> send(std::string_view data);

    /// Read and discard whatever the peer has sent. False once the peer has
    /// closed the connection or it has failed.
    [[nodiscard]] bool drain();

    /// Close the sending side first, so the peer reads an orderly end of stream
    /// after anything already sent, then release the socket.
    void close();

private:
    friend class TcpListener;
    explicit TcpConnection(SocketFd fd) : m_fd(fd) {}

    SocketFd m_fd = kInvalidSocket;
};

/// A non-blocking listening socket. Like UdpSocket, a failure to bind surfaces
/// through valid()/last_error() rather than throwing.
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();

    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    /// Listen on bind_ip:port; an empty bind_ip is INADDR_ANY. False on failure.
    bool open(const std::string& bind_ip, u16 port);

    void close();

    [[nodiscard]] bool valid() const { return m_fd != kInvalidSocket; }

    /// The next waiting connection, or an invalid one when nobody is waiting.
    [[nodiscard]] TcpConnection accept();

    [[nodiscard]] const std::string& last_error() const { return m_last_error; }

    /// The last open() failed because something else already listens there.
    [[nodiscard]] bool address_in_use() const { return m_address_in_use; }

private:
    SocketFd    m_fd = kInvalidSocket;
    std::string m_last_error;
    bool        m_address_in_use = false;
};

/// Process-wide network startup/teardown (WSAStartup/WSACleanup on Windows, a
/// no-op elsewhere). Refcounted, so safe to call in matched pairs.
bool startup();
void shutdown();

}  // namespace sm2::net
