#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <winsvc.h>
#endif

#include "common/logger.h"
#include "common/network_utils.h"
#include "guest_ipc.h"
#include "guest_config.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>

using namespace omnigpu;

#ifdef _WIN32
static std::atomic<bool> g_running{false};
static std::string g_host = "127.0.0.1";
static uint16_t g_port = 9443;

// Windows Service configuration
static const char* SERVICE_NAME = "OmniGPU_Guest";
static const char* SERVICE_DISPLAY = "OmniGPU Guest Daemon";
static SERVICE_STATUS g_service_status{};
static SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;

static void tcp_to_pipe_thread(SOCKET tcp_socket, HANDLE pipe) {
    std::vector<uint8_t> buffer(65536);
    while (g_running) {
        int r = ::recv(tcp_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
        if (r <= 0) {
            break;
        }
        DWORD w = 0;
        if (!WriteFile(pipe, buffer.data(), static_cast<DWORD>(r), &w, nullptr) || w != static_cast<DWORD>(r)) {
            break;
        }
    }
    ::closesocket(tcp_socket);
    CancelIoEx(pipe, nullptr);
}

static void client_thread_func(HANDLE pipe) {
    SPDLOG_INFO("Client connected to named pipe, connecting to host {}:{}", g_host, g_port);

    if (!tcp::init()) {
        SPDLOG_ERROR("Failed to initialize TCP stack for client");
        CloseHandle(pipe);
        return;
    }

    SOCKET tcp_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == INVALID_SOCKET) {
        SPDLOG_ERROR("Failed to create TCP socket for client");
        CloseHandle(pipe);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    if (::inet_pton(AF_INET, g_host.c_str(), &addr.sin_addr) != 1) {
        SPDLOG_ERROR("Invalid host address: {}", g_host);
        ::closesocket(tcp_socket);
        CloseHandle(pipe);
        return;
    }

    if (::connect(tcp_socket, (sockaddr*)&addr, sizeof(addr)) != 0) {
        SPDLOG_ERROR("Failed to connect to host {}:{}", g_host, g_port);
        ::closesocket(tcp_socket);
        CloseHandle(pipe);
        return;
    }

    tcp::set_tcp_nodelay(tcp_socket);
    SPDLOG_INFO("Daemon established TCP connection to host for client");

    // Spawn thread to read from TCP and write to Pipe
    std::thread tcp_to_pipe(tcp_to_pipe_thread, tcp_socket, pipe);

    // Main thread reads from Pipe and writes to TCP
    std::vector<uint8_t> buffer(65536);
    while (g_running) {
        DWORD bytes_avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes_avail, nullptr)) {
            break; // Client disconnected or pipe closed
        }
        if (bytes_avail > 0) {
            DWORD r = 0;
            if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &r, nullptr) || r == 0) {
                break;
            }
            if (!tcp::send_all(tcp_socket, buffer.data(), static_cast<size_t>(r))) {
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    ::closesocket(tcp_socket);
    CancelIoEx(pipe, nullptr);
    if (tcp_to_pipe.joinable()) {
        tcp_to_pipe.join();
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    SPDLOG_INFO("Client disconnected, closed pipe and TCP socket");
}

static void stop_daemon() {
    g_running = false;
    // Connect to named pipe once to unblock ConnectNamedPipe
    HANDLE h = CreateFileA(ipc::kPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void pipe_server_func() {
    SECURITY_ATTRIBUTES sa{};
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;

    while (g_running) {
        HANDLE pipe = CreateNamedPipeA(
            ipc::kPipeName, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            SPDLOG_ERROR("Failed to create pipe instance: {}", GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (ConnectNamedPipe(pipe, nullptr)) {
            std::thread t(client_thread_func, pipe);
            t.detach();
        } else {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                CloseHandle(pipe);
            } else {
                std::thread t(client_thread_func, pipe);
                t.detach();
            }
        }
    }
}

// Windows Service Manager
static void update_service_status(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    g_service_status.dwCurrentState = state;
    g_service_status.dwWin32ExitCode = exitCode;
    g_service_status.dwWaitHint = waitHint;
    SetServiceStatus(g_service_status_handle, &g_service_status);
}

static void WINAPI service_ctrl(DWORD ctrl) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SPDLOG_INFO("Service: stop requested");
        update_service_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        stop_daemon();
        break;
    default:
        break;
    }
}

static void WINAPI service_main(DWORD, LPSTR*) {
    g_service_status_handle = RegisterServiceCtrlHandlerA(SERVICE_NAME, service_ctrl);
    if (!g_service_status_handle) return;

    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_service_status.dwServiceSpecificExitCode = 0;

    update_service_status(SERVICE_START_PENDING, NO_ERROR, 5000);

    // Read config
    auto cfg = config::load();
    g_host = cfg.host;
    g_port = cfg.port;

    g_running = true;

    update_service_status(SERVICE_RUNNING);

    pipe_server_func();

    g_running = false;
    update_service_status(SERVICE_STOPPED);
}

static bool run_as_service() {
    SERVICE_TABLE_ENTRYA table[] = {
        {const_cast<char*>(SERVICE_NAME), service_main},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherA(table) != 0;
}

static bool install_service() {
    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exePath, sizeof(exePath))) {
        SPDLOG_ERROR("Service install: GetModuleFileName failed");
        return false;
    }

    std::string cmdLine = "\"" + std::string(exePath) + "\" --service";

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        SPDLOG_ERROR("Service install: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    SC_HANDLE service = CreateServiceA(
        scm, SERVICE_NAME, SERVICE_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        cmdLine.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!service) {
        if (err == ERROR_SERVICE_EXISTS) {
            SPDLOG_INFO("Service '{}' already installed", SERVICE_NAME);
            return true;
        }
        SPDLOG_ERROR("Service install: CreateService failed (error {})", err);
        return false;
    }

    SPDLOG_INFO("Service '{}' installed successfully", SERVICE_NAME);
    return true;
}

static bool uninstall_service() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        SPDLOG_ERROR("Service uninstall: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (!service) {
        CloseServiceHandle(scm);
        SPDLOG_WARN("Service '{}' not found", SERVICE_NAME);
        return false;
    }

    SERVICE_STATUS ss;
    ControlService(service, SERVICE_CONTROL_STOP, &ss);

    if (!DeleteService(service)) {
        DWORD err = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        SPDLOG_ERROR("Service uninstall: DeleteService failed (error {})", err);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    SPDLOG_INFO("Service '{}' uninstalled", SERVICE_NAME);
    return true;
}

int main(int argc, char* argv[]) {
    omnigpu::init_logger();

    // Read config from json (env vars override inside config::load)
    auto cfg = config::load();
    g_host = cfg.host;
    g_port = cfg.port;

    if (argc > 1 && strcmp(argv[1], "--status") == 0) {
        BOOL exists = WaitNamedPipeA(ipc::kPipeName, 0);
        printf(exists ? "RUNNING\n" : "NOT RUNNING\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--stop") == 0) {
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE service = OpenServiceA(scm, SERVICE_NAME, SERVICE_STOP);
            if (service) {
                SERVICE_STATUS ss;
                if (ControlService(service, SERVICE_CONTROL_STOP, &ss)) {
                    printf("STOPPED\n");
                    CloseServiceHandle(service);
                    CloseServiceHandle(scm);
                    return 0;
                }
                CloseServiceHandle(service);
            }
            CloseServiceHandle(scm);
        }

        HANDLE h = CreateFileA(ipc::kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        system("taskkill /f /im omnigpu_guestd.exe 2>nul");
        printf("STOPPED\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--install") == 0) {
        return install_service() ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--uninstall") == 0) {
        return uninstall_service() ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--service") == 0) {
        if (run_as_service()) {
            return 0;
        }
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "--foreground") == 0) {
        printf("OmniGPU daemon (foreground) host=%s:%d\n", g_host.c_str(), g_port);
        g_running = true;
        pipe_server_func();
        g_running = false;
        return 0;
    }

    if (run_as_service()) {
        return 0;
    }

    // Spawn hidden process
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof(self));
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "\"%s\" --foreground", self);
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        printf("OmniGPU daemon started (PID %u)\n", static_cast<unsigned int>(pi.dwProcessId));
    }
    return 0;
}
#else
// =========================================================================
// Linux / macOS daemon — Unix Domain Socket server
// Equivalent of Windows Named Pipe server above.
// Socket path: /tmp/omnigpu_guest.sock
// =========================================================================
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <csignal>

using namespace omnigpu;

static std::atomic<bool> g_running{false};
static std::string g_host = "127.0.0.1";
static uint16_t g_port = 9443;

// Bridge one Unix socket client fd ↔ one TCP connection to Host
static void client_thread_func(int client_fd) {
    SPDLOG_INFO("Client connected via unix socket, connecting to host {}:{}", g_host, g_port);

    int tcp_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        SPDLOG_ERROR("Failed to create TCP socket: {}", strerror(errno));
        ::close(client_fd);
        return;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_port);
    if (::inet_pton(AF_INET, g_host.c_str(), &addr.sin_addr) != 1) {
        SPDLOG_ERROR("Invalid host address: {}", g_host);
        ::close(tcp_fd);
        ::close(client_fd);
        return;
    }

    if (::connect(tcp_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("Failed to connect to host {}:{} — {}", g_host, g_port, strerror(errno));
        ::close(tcp_fd);
        ::close(client_fd);
        return;
    }

    tcp::set_tcp_nodelay(tcp_fd);
    SPDLOG_INFO("Daemon bridging unix socket ↔ TCP {}:{}", g_host, g_port);

    // Thread: TCP → Unix socket
    std::thread tcp_to_uds([client_fd, tcp_fd]() {
        std::vector<uint8_t> buf(65536);
        while (g_running) {
            ssize_t r = ::recv(tcp_fd, buf.data(), buf.size(), 0);
            if (r <= 0) break;
            if (::write(client_fd, buf.data(), static_cast<size_t>(r)) < 0) break;
        }
        ::shutdown(client_fd, SHUT_WR);
    });

    // Main: Unix socket → TCP
    std::vector<uint8_t> buf(65536);
    while (g_running) {
        ssize_t r = ::read(client_fd, buf.data(), buf.size());
        if (r <= 0) break;
        if (!tcp::send_all(tcp_fd, buf.data(), static_cast<size_t>(r))) break;
    }

    ::close(tcp_fd);
    if (tcp_to_uds.joinable()) tcp_to_uds.join();
    ::close(client_fd);
    SPDLOG_INFO("Client disconnected, closed unix socket and TCP connection");
}

static void unix_socket_server_func() {
    // Remove stale socket file if it exists
    ::unlink(ipc::kSocketPath);

    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        SPDLOG_ERROR("Failed to create unix server socket: {}", strerror(errno));
        return;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, ipc::kSocketPath, sizeof(addr.sun_path) - 1);

    if (::bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("Failed to bind unix socket {}: {}", ipc::kSocketPath, strerror(errno));
        ::close(server_fd);
        return;
    }

    // Allow any local user to connect (same as Windows open DACL)
    ::chmod(ipc::kSocketPath, 0777);

    if (::listen(server_fd, 16) < 0) {
        SPDLOG_ERROR("Failed to listen on unix socket: {}", strerror(errno));
        ::close(server_fd);
        ::unlink(ipc::kSocketPath);
        return;
    }

    SPDLOG_INFO("Unix socket server listening at {}", ipc::kSocketPath);

    while (g_running) {
        int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            if (g_running) SPDLOG_ERROR("accept() failed: {}", strerror(errno));
            break;
        }
        std::thread t(client_thread_func, client_fd);
        t.detach();
    }

    ::close(server_fd);
    ::unlink(ipc::kSocketPath);
    SPDLOG_INFO("Unix socket server stopped");
}

int main(int argc, char* argv[]) {
    omnigpu::init_logger();

    auto cfg = config::load();
    g_host = cfg.host;
    g_port = cfg.port;

    if (argc > 1 && strcmp(argv[1], "--status") == 0) {
        // Try to connect briefly to detect if daemon is running
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        ::strncpy(addr.sun_path, ipc::kSocketPath, sizeof(addr.sun_path) - 1);
        bool running = (fd >= 0 && ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        if (fd >= 0) ::close(fd);
        printf(running ? "RUNNING\n" : "NOT RUNNING\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--stop") == 0) {
        // Signal the daemon to stop by removing the socket file
        system("pkill -f omnigpu_guestd 2>/dev/null");
        printf("STOPPED\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--foreground") == 0) {
        printf("OmniGPU daemon (foreground) host=%s:%d\n", g_host.c_str(), g_port);
        signal(SIGTERM, [](int) { g_running = false; });
        signal(SIGINT,  [](int) { g_running = false; });
        g_running = true;
        unix_socket_server_func();
        return 0;
    }

    // Default: daemonize and run in background
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork() failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid > 0) {
        // Parent: print PID and exit
        printf("OmniGPU daemon started (PID %d)\n", pid);
        return 0;
    }

    // Child: run as background daemon
    setsid();
    signal(SIGTERM, [](int) { g_running = false; });
    signal(SIGINT,  [](int) { g_running = false; });
    g_running = true;
    unix_socket_server_func();
    return 0;
}

#endif

