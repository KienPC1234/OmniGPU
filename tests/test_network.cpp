#include "common/network_utils.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <gtest/gtest.h>

namespace {

class TcpRuntime final {
public:
    TcpRuntime() : initialized_(omnigpu::tcp::init()) {}
    ~TcpRuntime() { if (initialized_) omnigpu::tcp::cleanup(); }
    bool initialized() const { return initialized_; }
private:
    bool initialized_;
};

void close_if_valid(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        omnigpu::tcp::shutdown_socket(socket);
        omnigpu::tcp::close_socket(socket);
        socket = INVALID_SOCKET;
    }
}

bool wait_readable(SOCKET socket, long timeout_seconds) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval timeout{};
    timeout.tv_sec = timeout_seconds;
#ifdef _WIN32
    return ::select(0, &read_set, nullptr, nullptr, &timeout) == 1;
#else
    return ::select(socket + 1, &read_set, nullptr, nullptr, &timeout) == 1;
#endif
}

} // namespace

TEST(Network, RejectsNullBuffers) {
    EXPECT_FALSE(omnigpu::tcp::send_all(INVALID_SOCKET, nullptr, 1));
    EXPECT_FALSE(omnigpu::tcp::recv_all(INVALID_SOCKET, nullptr, 1));
    EXPECT_TRUE(omnigpu::tcp::send_all(INVALID_SOCKET, nullptr, 0));
    EXPECT_TRUE(omnigpu::tcp::recv_all(INVALID_SOCKET, nullptr, 0));
    EXPECT_FALSE(omnigpu::tcp::recv_all_for(INVALID_SOCKET, nullptr, 1, 100));
    EXPECT_TRUE(omnigpu::tcp::recv_all_for(INVALID_SOCKET, nullptr, 0, 100));
}

TEST(Network, LoopbackRoundTrip) {
    TcpRuntime runtime;
    ASSERT_TRUE(runtime.initialized());

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(listener, INVALID_SOCKET);

    int reuse = 1;
    ASSERT_EQ(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                          reinterpret_cast<const char*>(&reuse), sizeof(reuse)), 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    ASSERT_EQ(::listen(listener, 1), 0);

#ifdef _WIN32
    int address_length = sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                            &address_length), 0);

    std::array<uint8_t, 5> server_received{};
    std::array<uint8_t, 5> client_received{};
    bool client_ok = false;
    std::jthread client([&]() {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) return;
        if (::connect(socket, reinterpret_cast<sockaddr*>(&address),
                      sizeof(address)) != 0) {
            close_if_valid(socket);
            return;
        }
        omnigpu::tcp::set_tcp_timeout(socket, 5);
        const std::array<uint8_t, 5> request{{1, 2, 3, 4, 5}};
        client_ok = omnigpu::tcp::send_all(socket, request.data(), request.size()) &&
                    omnigpu::tcp::recv_all(socket, client_received.data(),
                                           client_received.size());
        close_if_valid(socket);
    });

    ASSERT_TRUE(wait_readable(listener, 5));
    SOCKET accepted = ::accept(listener, nullptr, nullptr);
    ASSERT_NE(accepted, INVALID_SOCKET);
    ASSERT_TRUE(omnigpu::tcp::set_tcp_timeout(accepted, 5));
    ASSERT_TRUE(omnigpu::tcp::recv_all(accepted, server_received.data(),
                                       server_received.size()));
    const std::array<uint8_t, 5> response{{9, 8, 7, 6, 5}};
    ASSERT_TRUE(omnigpu::tcp::send_all(accepted, response.data(), response.size()));

    close_if_valid(accepted);
    close_if_valid(listener);
    client.join();

    EXPECT_TRUE(client_ok);
    EXPECT_EQ(server_received, (std::array<uint8_t, 5>{{1, 2, 3, 4, 5}}));
    EXPECT_EQ(client_received, response);
}

TEST(Network, TimeoutCanBeCleared) {
    TcpRuntime runtime;
    ASSERT_TRUE(runtime.initialized());
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(socket, INVALID_SOCKET);

    ASSERT_TRUE(omnigpu::tcp::set_tcp_timeout(socket, 1));
    ASSERT_TRUE(omnigpu::tcp::set_tcp_timeout(socket, 0));

#ifdef _WIN32
    DWORD value = 1;
    int length = sizeof(value);
    ASSERT_EQ(::getsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<char*>(&value), &length), 0);
    EXPECT_EQ(value, 0U);
#else
    timeval value{};
    socklen_t length = sizeof(value);
    ASSERT_EQ(::getsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, &length), 0);
    EXPECT_EQ(value.tv_sec, 0);
    EXPECT_EQ(value.tv_usec, 0);
#endif

    close_if_valid(socket);
}


TEST(Network, OverallReceiveDeadlineRejectsSlowDrip) {
    using namespace std::chrono_literals;
    TcpRuntime runtime;
    ASSERT_TRUE(runtime.initialized());

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(listener, INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)), 0);
    ASSERT_EQ(::listen(listener, 1), 0);
#ifdef _WIN32
    int address_length = sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                            &address_length), 0);

    std::jthread server([&]() {
        SOCKET accepted = ::accept(listener, nullptr, nullptr);
        if (accepted == INVALID_SOCKET) return;
        for (uint8_t byte = 1; byte <= 4; ++byte) {
            if (!omnigpu::tcp::send_all(accepted, &byte, 1)) break;
            std::this_thread::sleep_for(75ms);
        }
        close_if_valid(accepted);
    });

    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(socket, INVALID_SOCKET);
    ASSERT_EQ(::connect(socket, reinterpret_cast<sockaddr*>(&address),
                        sizeof(address)), 0);
    std::array<uint8_t, 4> bytes{};
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(omnigpu::tcp::recv_all_for(socket, bytes.data(), bytes.size(),
                                            100));
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 500ms);
    close_if_valid(socket);
    close_if_valid(listener);
}
