#include "service_manager.h"
#include "server.h"
#include "config.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <winsvc.h>

namespace {

const char* SERVICE_NAME = "OmniGPUHost";
const char* SERVICE_DISPLAY = "OmniGPU Host Server";

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
omnigpu::Server* g_serviceServer = nullptr;

void update_status(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHint;
    SetServiceStatus(g_statusHandle, &g_status);
}

void WINAPI service_ctrl(DWORD ctrl) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SPDLOG_INFO("Service: stop requested");
        update_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (g_serviceServer) {
            g_serviceServer->stop();
        }
        break;
    default:
        break;
    }
}

void WINAPI service_main(DWORD, LPSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerA(SERVICE_NAME, service_ctrl);
    if (!g_statusHandle) return;

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwServiceSpecificExitCode = 0;

    update_status(SERVICE_START_PENDING, NO_ERROR, 5000);

    omnigpu::HostConfig config;
    omnigpu::load_config(config);

    omnigpu::Server server(config);
    g_serviceServer = &server;

    if (!server.start()) {
        update_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        g_serviceServer = nullptr;
        return;
    }

    update_status(SERVICE_RUNNING);
    server.run();
    g_serviceServer = nullptr;

    update_status(SERVICE_STOPPED);
}

} // anonymous namespace
#endif

namespace omnigpu {

#ifdef _WIN32

bool install_service() {
    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exePath, sizeof(exePath))) {
        SPDLOG_ERROR("Service install: GetModuleFileName failed");
        return false;
    }

    std::string cmdLine = "\"" + std::string(exePath) + "\" --service";

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        SPDLOG_ERROR("Service install: OpenSCManager failed (error {})",
                     GetLastError());
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

bool uninstall_service() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        SPDLOG_ERROR("Service uninstall: OpenSCManager failed (error {})",
                     GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, SERVICE_NAME,
                                      SERVICE_STOP | DELETE);
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

bool run_as_service() {
    SERVICE_TABLE_ENTRYA table[] = {
        {const_cast<char*>(SERVICE_NAME), service_main},
        {nullptr, nullptr},
    };

    if (!StartServiceCtrlDispatcherA(table)) {
        SPDLOG_ERROR("Service: StartServiceCtrlDispatcher failed (error {})",
                     GetLastError());
        return false;
    }

    return true;
}

#else
// Linux services run in the foreground under systemd. Installation requires
// creating the service user, configuration, and unit file, which is handled by
// the idempotent installer shipped with the project.
bool install_service() {
    SPDLOG_ERROR("Linux service installation requires: sudo ./scripts/linux/install_host.sh --start");
    return false;
}

bool uninstall_service() {
    SPDLOG_ERROR("Linux service removal requires: sudo ./scripts/linux/install_host.sh --uninstall");
    return false;
}

bool run_as_service() {
    SPDLOG_INFO("Running in foreground service mode; systemd owns daemonization");
    return false;
}
#endif

} // namespace omnigpu
