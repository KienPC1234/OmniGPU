#include "guest_ipc.h"
#include <cstdio>
#include <cstring>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace omnigpu::ipc {

#ifdef _WIN32

int connect_to_daemon() {
    HANDLE pipe = CreateFileA(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        SPDLOG_DEBUG("Daemon not available (pipe connect failed: {})",
                     GetLastError());
        return -1;
    }

    SPDLOG_INFO("Connected to OmniGPU daemon via named pipe");
    return static_cast<int>(reinterpret_cast<intptr_t>(pipe));
}

bool send_ipc_message(int pipe_handle, IpcMessageType type,
                       uint32_t session_id, const void* data, uint32_t size) {
    if (pipe_handle < 0) return false;

    HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pipe_handle));
    IpcHeader header{type, size, session_id};
    DWORD written = 0;

    if (!WriteFile(pipe, &header, sizeof(header), &written, nullptr) ||
        written != sizeof(header)) {
        SPDLOG_ERROR("Failed to write IPC header: {}", GetLastError());
        return false;
    }

    if (size > 0 && data) {
        if (!WriteFile(pipe, data, size, &written, nullptr) ||
            written != size) {
            SPDLOG_ERROR("Failed to write IPC payload: {}", GetLastError());
            return false;
        }
    }

    return true;
}

bool recv_ipc_header(int pipe_handle, IpcHeader& header) {
    if (pipe_handle < 0) return false;

    HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pipe_handle));
    DWORD read_bytes = 0;

    if (!ReadFile(pipe, &header, sizeof(header), &read_bytes, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_LISTENING && err != ERROR_NO_DATA) {
            SPDLOG_ERROR("Failed to read IPC header: {}", err);
        }
        return false;
    }

    return read_bytes == sizeof(header);
}

bool recv_ipc_data(int pipe_handle, void* buffer, uint32_t size) {
    if (pipe_handle < 0 || !buffer || size == 0) return false;

    HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pipe_handle));
    DWORD read_bytes = 0;

    if (!ReadFile(pipe, buffer, size, &read_bytes, nullptr)) {
        SPDLOG_ERROR("Failed to read IPC data: {}", GetLastError());
        return false;
    }

    return read_bytes == size;
}

void close_pipe(int pipe_handle) {
    if (pipe_handle < 0) return;
    HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pipe_handle));
    CloseHandle(pipe);
    SPDLOG_DEBUG("Disconnected from OmniGPU daemon");
}

bool is_daemon_running() {
    HANDLE pipe = CreateFileA(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) return false;
    CloseHandle(pipe);
    return true;
}

uint32_t create_session(int pipe_handle) {
    static uint32_t next_session = 1;
    uint32_t sid = next_session++;

    IpcHeader header{IpcMessageType::kConnect, 0, sid};
    DWORD written = 0;
    HANDLE pipe = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pipe_handle));

    if (!WriteFile(pipe, &header, sizeof(header), &written, nullptr)) {
        SPDLOG_ERROR("Failed to create daemon session: {}", GetLastError());
        return 0;
    }

    // Wait for connected response
    IpcHeader resp{};
    if (!recv_ipc_header(pipe_handle, resp) ||
        resp.type != IpcMessageType::kConnected) {
        SPDLOG_WARN("Daemon session not confirmed");
        return 0;
    }

    SPDLOG_INFO("Daemon session {} created", sid);
    return sid;
}

#else
// -------------------------------------------------------------------------
// Linux / macOS: Unix Domain Socket (equivalent of Windows Named Pipe).
// The socket file lives at kSocketPath (/tmp/omnigpu_guest.sock).
// No TCP port is used — communication stays local to the machine.
// -------------------------------------------------------------------------
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>

int connect_to_daemon() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        SPDLOG_DEBUG("Unix socket creation failed: {}", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_DEBUG("Daemon not available (unix socket connect failed: {})", strerror(errno));
        ::close(fd);
        return -1;
    }

    SPDLOG_INFO("Connected to OmniGPU daemon via unix socket ({})", kSocketPath);
    return fd;
}

bool send_ipc_message(int pipe_handle, IpcMessageType type,
                       uint32_t session_id, const void* data, uint32_t size) {
    if (pipe_handle < 0) return false;

    IpcHeader header{type, size, session_id};

    if (::write(pipe_handle, &header, sizeof(header)) != static_cast<ssize_t>(sizeof(header))) {
        SPDLOG_ERROR("Failed to write IPC header: {}", strerror(errno));
        return false;
    }

    if (size > 0 && data) {
        if (::write(pipe_handle, data, size) != static_cast<ssize_t>(size)) {
            SPDLOG_ERROR("Failed to write IPC payload: {}", strerror(errno));
            return false;
        }
    }

    return true;
}

bool recv_ipc_header(int pipe_handle, IpcHeader& header) {
    if (pipe_handle < 0) return false;

    ssize_t n = ::read(pipe_handle, &header, sizeof(header));
    if (n <= 0) {
        if (n < 0) SPDLOG_ERROR("Failed to read IPC header: {}", strerror(errno));
        return false;
    }
    return static_cast<size_t>(n) == sizeof(header);
}

bool recv_ipc_data(int pipe_handle, void* buffer, uint32_t size) {
    if (pipe_handle < 0 || !buffer || size == 0) return false;

    size_t total = 0;
    while (total < size) {
        ssize_t n = ::read(pipe_handle,
                           static_cast<uint8_t*>(buffer) + total,
                           size - total);
        if (n <= 0) {
            SPDLOG_ERROR("Failed to read IPC data: {}", strerror(errno));
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

void close_pipe(int pipe_handle) {
    if (pipe_handle < 0) return;
    ::close(pipe_handle);
    SPDLOG_DEBUG("Disconnected from OmniGPU daemon (unix socket)");
}

bool is_daemon_running() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);

    bool running = (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(fd);
    return running;
}

uint32_t create_session(int pipe_handle) {
    static uint32_t next_session = 1;
    uint32_t sid = next_session++;

    IpcHeader header{IpcMessageType::kConnect, 0, sid};
    if (::write(pipe_handle, &header, sizeof(header)) != static_cast<ssize_t>(sizeof(header))) {
        SPDLOG_ERROR("Failed to create daemon session: {}", strerror(errno));
        return 0;
    }

    // Wait for kConnected response
    IpcHeader resp{};
    if (!recv_ipc_header(pipe_handle, resp) ||
        resp.type != IpcMessageType::kConnected) {
        SPDLOG_WARN("Daemon session not confirmed");
        return 0;
    }

    SPDLOG_INFO("Daemon session {} created", sid);
    return sid;
}

#endif

} // namespace omnigpu::ipc

