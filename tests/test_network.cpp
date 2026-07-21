#include <gtest/gtest.h>
#include "common/network_utils.h"

using namespace omnigpu;

// Verify network utility functions compile and basic invariants hold.
// Full send/recv tests require a running server.

TEST(Network, InitCleanup) {
    EXPECT_TRUE(tcp::init());
    tcp::cleanup();
}

TEST(Network, TcpNoDelay) {
    EXPECT_TRUE(tcp::init());
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(fd, INVALID_SOCKET);
    EXPECT_TRUE(tcp::set_tcp_nodelay(fd));
    EXPECT_TRUE(tcp::set_tcp_keepalive(fd));
    tcp::close_socket(fd);
    tcp::cleanup();
}

TEST(Network, TcpTimeout) {
    EXPECT_TRUE(tcp::init());
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(fd, INVALID_SOCKET);
    EXPECT_TRUE(tcp::set_tcp_timeout(fd, 30));
    tcp::close_socket(fd);
    tcp::cleanup();
}
