#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#define NOMB
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* kPipeName = "\\\\.\\pipe\\OmniGPU_Guest";

static bool send_cmd(const char* cmd, char* resp, DWORD resp_size) {
    HANDLE h = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(h, cmd, (DWORD)strlen(cmd) + 1, &w, NULL);
    ReadFile(h, resp, resp_size, &resp_size, NULL);
    CloseHandle(h);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("OmniGPU Guest Control\n");
        printf("Usage: omnigpu_guestctl <command>\n\n");
        printf("Commands:\n");
        printf("  status     Check daemon status\n");
        printf("  start      Start daemon\n");
        printf("  stop       Stop daemon\n");
        printf("  install    Install as Windows service\n");
        printf("  uninstall  Remove Windows service\n");
        printf("  config     Show current configuration\n");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        HANDLE h = CreateFileA(kPipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        printf(h != INVALID_HANDLE_VALUE ? "RUNNING\n" : "NOT RUNNING\n");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, sizeof(path));
        // Find the daemon exe (same dir as ctl)
        char* p = strrchr(path, '\\');
        if (p) *p = 0;
        strcat(path, "\\omnigpu_guestd.exe");

        if (CreateProcessA(path, (char*)"omnigpu_guestd.exe --foreground",
            NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            printf("Daemon started (PID %u)\n", pi.dwProcessId);
        } else {
            printf("Failed to start daemon: %lu\n", GetLastError());
        }
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0) {
        // Connect to pipe with write access to signal stop
        HANDLE h = CreateFileA(kPipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            char stop = 0;
            DWORD w = 0;
            WriteFile(h, &stop, 1, &w, NULL);
            CloseHandle(h);
        }
        // Also kill process
        system("taskkill /f /im omnigpu_guestd.exe 2>nul");
        printf("Daemon stopped\n");
        return 0;
    }

    if (strcmp(argv[1], "install") == 0) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, sizeof(path));
        char* p = strrchr(path, '\\');
        if (p) *p = 0;
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
            "sc create OmniGPU_Guest binPath= \"\\\"%s\\omnigpu_guestd.exe\\\" --foreground\" start= auto DisplayName= \"OmniGPU Guest Daemon\"",
            path);
        system(cmd);
        printf("Service installed. Start with: net start OmniGPU_Guest\n");
        return 0;
    }

    if (strcmp(argv[1], "uninstall") == 0) {
        system("net stop OmniGPU_Guest 2>nul");
        system("sc delete OmniGPU_Guest 2>nul");
        printf("Service removed\n");
        return 0;
    }

    if (strcmp(argv[1], "config") == 0) {
        printf("Configuration:\n");
        printf("  OMNIGPU_HOST = %s\n", getenv("OMNIGPU_HOST") ? getenv("OMNIGPU_HOST") : "(not set)");
        printf("  OMNIGPU_PORT = %s\n", getenv("OMNIGPU_PORT") ? getenv("OMNIGPU_PORT") : "(not set, default 9443)");
        printf("  Pipe: %s\n", kPipeName);
        return 0;
    }

    return 0;
}
