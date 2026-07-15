#pragma once

#include <cstdint>
#include <string>

namespace omnigpu::ipc {

// Named pipe name for daemon communication
constexpr const char* kPipeName = "\\\\.\\pipe\\OmniGPU_Guest";

// IPC message types
enum class IpcMessageType : uint32_t {
    kInvalid = 0,
    kConnect,       // Guest → Daemon: request connection for a session
    kConnected,     // Daemon → Guest: connection established
    kData,          // Bidirectional: raw data to/from host TCP
    kDisconnect,    // Guest → Daemon: end session
    kStatus,        // Guest → Daemon: query status
    kStatusResponse,// Daemon → Guest: status info
    kError,         // Daemon → Guest: error occurred
};

// IPC message header (followed by payload data)
struct IpcHeader {
    IpcMessageType type;
    uint32_t size;        // Size of payload following this header
    uint32_t session_id;  // Session ID for multiplexing
};

// Open a named pipe connection to the daemon
// Returns a pipe handle (HANDLE) or INVALID_HANDLE_VALUE on failure.
int connect_to_daemon();

// Send an IPC message with optional payload
bool send_ipc_message(int pipe_handle, IpcMessageType type,
                      uint32_t session_id, const void* data, uint32_t size);

// Receive an IPC message header (blocking)
bool recv_ipc_header(int pipe_handle, IpcHeader& header);

// Receive IPC payload data
bool recv_ipc_data(int pipe_handle, void* buffer, uint32_t size);

// Close pipe connection
void close_pipe(int pipe_handle);

// Check if daemon is running
bool is_daemon_running();

// Daemon session management
uint32_t create_session(int pipe_handle);
bool destroy_session(int pipe_handle, uint32_t session_id);

} // namespace omnigpu::ipc
