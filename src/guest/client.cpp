#include "client.h"
#include "guest_ipc.h"
#include <spdlog/spdlog.h>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace omnigpu {

Client::Client(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

Client::~Client() { disconnect(); }

void Client::set_ipc_pipe_handle(int pipe_handle, uint32_t session_id) {
    ipc_pipe_handle_ = pipe_handle;
    ipc_session_id_ = session_id;
}

bool Client::connect() {
    if (ipc_pipe_handle_ >= 0) {
        SPDLOG_INFO("Connected via daemon");
        return true;
    }

    SPDLOG_INFO("Connecting to {}:{}...", host_, port_);

    if (!tcp::init()) return false;
    tcpInitialized_ = true;

    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        SPDLOG_ERROR("socket creation failed: {}", tcp::last_error());
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        SPDLOG_ERROR("invalid address '{}': {}", host_, tcp::last_error());
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (::connect(socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        SPDLOG_ERROR("connect failed: {}", tcp::last_error());
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    tcp::set_tcp_nodelay(socket_);
    SPDLOG_INFO("Connected to {}:{}", host_, port_);
    return true;
}

void Client::disconnect() {
    if (ipc_pipe_handle_ >= 0) {
        ipc::close_pipe(ipc_pipe_handle_);
        ipc_pipe_handle_ = -1;
    }
    if (socket_ != INVALID_SOCKET) {
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (tcpInitialized_) {
        tcp::cleanup();
        tcpInitialized_ = false;
    }
    SPDLOG_INFO("Disconnected");
}

bool Client::send_data(const uint8_t* data, size_t size) {
    if (ipc_pipe_handle_ >= 0) {
#ifdef _WIN32
        DWORD w = 0;
        HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(ipc_pipe_handle_));
        return WriteFile(pipe, data, static_cast<DWORD>(size), &w, nullptr) && w == size;
#else
        ssize_t written = ::write(ipc_pipe_handle_, data, size);
        return written == static_cast<ssize_t>(size);
#endif
    }
    return tcp::send_all(socket_, data, size);
}

bool Client::receive_data(uint8_t* buffer, size_t size) {
    if (ipc_pipe_handle_ >= 0) {
#ifdef _WIN32
        DWORD r = 0;
        HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(ipc_pipe_handle_));
        return ReadFile(pipe, buffer, static_cast<DWORD>(size), &r, nullptr) && r == size;
#else
        size_t total = 0;
        while (total < size) {
            ssize_t r = ::read(ipc_pipe_handle_, buffer + total, size - total);
            if (r <= 0) return false;
            total += static_cast<size_t>(r);
        }
        return true;
#endif
    }
    return tcp::recv_all(socket_, buffer, size);
}

uint64_t Client::sync_query(uint64_t func_id, uint64_t arg) {
    uint32_t net_sz = htonl(16);
    uint8_t req[20];
    std::memcpy(req, &net_sz, 4);
    std::memcpy(req + 4, &func_id, 8);
    std::memcpy(req + 12, &arg, 8);
    if (!send_data(req, sizeof(req))) {
        SPDLOG_ERROR("sync_query: send failed");
        return 0;
    }
    uint64_t result = 0;
    if (!receive_data(reinterpret_cast<uint8_t*>(&result), sizeof(result))) {
        SPDLOG_ERROR("sync_query: recv failed");
        return 0;
    }
    return result;
}

} // namespace omnigpu
