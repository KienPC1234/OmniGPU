#pragma once

#include "common/network_utils.h"
#include <cstdint>
#include <string>
#include <vector>

namespace omnigpu {

class Client {
public:
    explicit Client(const std::string& host, uint16_t port);
    ~Client();

    bool connect();
    void disconnect();
    bool send_data(const uint8_t* data, size_t size);
    bool receive_data(uint8_t* buffer, size_t size);

    // Synchronous query: send query data, wait for uint64_t response
    // Used by vkGetBufferDeviceAddress and similar query functions
    uint64_t sync_query(uint64_t func_id, uint64_t arg);

    SOCKET socket() const { return socket_; }
    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

    // IPC mode: use daemon via named pipe (takes priority over TCP)
    void set_ipc_pipe_handle(int pipe_handle, uint32_t session_id);
    int ipc_pipe_handle() const { return ipc_pipe_handle_; }
    uint32_t ipc_session_id() const { return ipc_session_id_; }
    bool using_ipc() const { return ipc_pipe_handle_ >= 0; }

private:
    std::string host_;
    uint16_t port_;
    SOCKET socket_ = INVALID_SOCKET;
    bool tcpInitialized_ = false;

    // IPC via daemon (optional, replaces direct TCP)
    int ipc_pipe_handle_ = -1;
    uint32_t ipc_session_id_ = 0;
};

} // namespace omnigpu
