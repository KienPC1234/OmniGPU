#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winsvc.h>

#include "common/logger.h"
#include "common/network_utils.h"
#include "guest_ipc.h"
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
    char env_buf[256] = {};
    if (GetEnvironmentVariableA("OMNIGPU_HOST", env_buf, sizeof(env_buf)) && env_buf[0]) g_host = env_buf;
    if (GetEnvironmentVariableA("OMNIGPU_PORT", env_buf, sizeof(env_buf)) && env_buf[0]) g_port = (uint16_t)std::atoi(env_buf);

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

    // Read config from env
    char env_buf[256] = {};
    if (GetEnvironmentVariableA("OMNIGPU_HOST", env_buf, sizeof(env_buf)) && env_buf[0]) g_host = env_buf;
    if (GetEnvironmentVariableA("OMNIGPU_PORT", env_buf, sizeof(env_buf)) && env_buf[0]) g_port = (uint16_t)std::atoi(env_buf);

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
