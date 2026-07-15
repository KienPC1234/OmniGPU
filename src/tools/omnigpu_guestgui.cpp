#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <winsvc.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const char* kPipeName = "\\\\.\\pipe\\OmniGPU_Guest";
static HWND g_hStatus = NULL, g_hLog = NULL, g_hHost = NULL, g_hPort = NULL;

static HBRUSH g_hbrBackground = NULL;
static HBRUSH g_hbrEditBackground = NULL;

static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontLog = NULL;

static bool is_daemon_running() {
    return WaitNamedPipeA(kPipeName, 0) != FALSE;
}

static std::string get_service_status_str() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return is_daemon_running() ? "RUNNING (process)" : "STOPPED";
    }
    SC_HANDLE service = OpenServiceA(scm, "OmniGPU_Guest", SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scm);
        return is_daemon_running() ? "RUNNING (process)" : "NOT INSTALLED";
    }
    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
        std::string status = "STOPPED";
        if (ssp.dwCurrentState == SERVICE_RUNNING) status = "RUNNING (service)";
        else if (ssp.dwCurrentState == SERVICE_STOPPED) status = "STOPPED";
        else if (ssp.dwCurrentState == SERVICE_START_PENDING) status = "STARTING...";
        else if (ssp.dwCurrentState == SERVICE_STOP_PENDING) status = "STOPPING...";
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return status;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return "STOPPED";
}

static void refresh_status() {
    std::string running = get_service_status_str();
    SetWindowTextA(g_hStatus, running.c_str());

    char buf[64] = {};
    DWORD len = sizeof(buf);
    if (GetEnvironmentVariableA("OMNIGPU_HOST", buf, len) && buf[0]) {
        SetWindowTextA(g_hHost, buf);
    } else {
        SetWindowTextA(g_hHost, "127.0.0.1");
    }

    if (GetEnvironmentVariableA("OMNIGPU_PORT", buf, len) && buf[0]) {
        SetWindowTextA(g_hPort, buf);
    } else {
        SetWindowTextA(g_hPort, "9443");
    }
}

static void refresh_log() {
    char temp_path[MAX_PATH];
    std::string log_file_path;
    if (GetTempPathA(sizeof(temp_path), temp_path)) {
        log_file_path = std::string(temp_path) + "omnigpu_guest.log";
    } else {
        log_file_path = "omnigpu_guest.log";
    }

    FILE* f = fopen(log_file_path.c_str(), "r");
    if (!f) { SetWindowTextA(g_hLog, "(no log file)"); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 32768) { fseek(f, size - 32768, SEEK_SET); size = 32768; }
    std::string buf(size + 1, 0);
    fread(&buf[0], 1, size, f);
    fclose(f);
    SetWindowTextA(g_hLog, buf.c_str());
    SendMessageA(g_hLog, EM_LINESCROLL, 0, 0xFFFF);
}

static void start_daemon() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char* p = strrchr(path, '\\');
    if (p) *p = 0;
    strcat(path, "\\omnigpu_guestd.exe");
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(path, (char*)"omnigpu_guestd.exe --foreground",
        NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void stop_daemon() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char* p = strrchr(path, '\\');
    if (p) *p = 0;
    strcat(path, "\\omnigpu_guestd.exe");
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(path, (char*)"omnigpu_guestd.exe --stop",
        NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void start_service_async() {
    std::thread([]() {
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE service = OpenServiceA(scm, "OmniGPU_Guest", SERVICE_START);
            if (service) {
                StartServiceA(service, 0, nullptr);
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                return;
            }
            CloseServiceHandle(scm);
        }
        start_daemon();
    }).detach();
}

static void stop_service_async() {
    std::thread([]() {
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE service = OpenServiceA(scm, "OmniGPU_Guest", SERVICE_STOP);
            if (service) {
                SERVICE_STATUS ss;
                ControlService(service, SERVICE_CONTROL_STOP, &ss);
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                return;
            }
            CloseServiceHandle(scm);
        }
        stop_daemon();
    }).detach();
}

static void restart_service_async() {
    std::thread([]() {
        SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE service = OpenServiceA(scm, "OmniGPU_Guest", SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
            if (service) {
                SERVICE_STATUS ss;
                ControlService(service, SERVICE_CONTROL_STOP, &ss);
                for (int i = 0; i < 10; ++i) {
                    SERVICE_STATUS_PROCESS ssp;
                    DWORD needed;
                    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
                        if (ssp.dwCurrentState == SERVICE_STOPPED) break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                StartServiceA(service, 0, nullptr);
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                return;
            }
            CloseServiceHandle(scm);
        }
        stop_daemon();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        start_daemon();
    }).detach();
}

static void CALLBACK on_timer(HWND, UINT, UINT_PTR, DWORD) {
    refresh_status();
    refresh_log();
}

static HWND create_control(const char* className, const char* text, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HFONT font) {
    HWND hw = CreateWindowA(className, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, menu, nullptr, nullptr);
    if (font) {
        SendMessageA(hw, WM_SETFONT, (WPARAM)font, TRUE);
    }
    return hw;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hbrBackground = CreateSolidBrush(RGB(18, 18, 18));      // Premium Dark Background
        g_hbrEditBackground = CreateSolidBrush(RGB(30, 30, 30));  // Darker gray edit controls

        g_hFontNormal = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hFontBold = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hFontLog = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

        // Row 1: Status
        create_control("STATIC", "Status:", 0, 10, 10, 50, 20, hWnd, NULL, g_hFontBold);
        g_hStatus = create_control("STATIC", "...", 0, 60, 10, 110, 20, hWnd, NULL, g_hFontNormal);
        create_control("BUTTON", "Start", 0, 180, 8, 60, 23, hWnd, (HMENU)100, g_hFontNormal);
        create_control("BUTTON", "Stop", 0, 245, 8, 60, 23, hWnd, (HMENU)101, g_hFontNormal);
        create_control("BUTTON", "Restart", 0, 310, 8, 65, 23, hWnd, (HMENU)104, g_hFontNormal);
        create_control("BUTTON", "Refresh", 0, 380, 8, 65, 23, hWnd, (HMENU)102, g_hFontNormal);

        // Row 2: Host / Port configuration
        create_control("STATIC", "Host:", 0, 10, 38, 40, 20, hWnd, NULL, g_hFontBold);
        g_hHost = create_control("EDIT", "", WS_BORDER | ES_AUTOHSCROLL, 50, 36, 125, 20, hWnd, NULL, g_hFontNormal);
        create_control("STATIC", "Port:", 0, 185, 38, 35, 20, hWnd, NULL, g_hFontBold);
        g_hPort = create_control("EDIT", "", WS_BORDER | ES_NUMBER, 225, 36, 60, 20, hWnd, NULL, g_hFontNormal);
        create_control("BUTTON", "Apply", 0, 310, 34, 65, 23, hWnd, (HMENU)103, g_hFontNormal);

        // Row 3: Log viewer
        g_hLog = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 68, 465, 230, hWnd, NULL, nullptr, nullptr);
        SendMessageA(g_hLog, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);

        SetTimer(hWnd, 1, 2000, on_timer);
        refresh_status();
        refresh_log();
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, RGB(224, 224, 224));
        SetBkColor(hdc, RGB(18, 18, 18));
        return (INT_PTR)g_hbrBackground;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(30, 30, 30));
        return (INT_PTR)g_hbrEditBackground;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 100: start_service_async(); break;
        case 101: stop_service_async(); break;
        case 104: restart_service_async(); break;
        case 102: refresh_status(); refresh_log(); break;
        case 103: {
            char host[256] = {}, port[64] = {};
            GetWindowTextA(g_hHost, host, sizeof(host));
            GetWindowTextA(g_hPort, port, sizeof(port));
            SetEnvironmentVariableA("OMNIGPU_HOST", host);
            SetEnvironmentVariableA("OMNIGPU_PORT", port);
            MessageBoxA(hWnd, "Variables set. Restart service to apply.", "Config", MB_OK);
            break;
        }
        }
        return 0;
    case WM_CLOSE:
        KillTimer(hWnd, 1);
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        DeleteObject(g_hbrBackground);
        DeleteObject(g_hbrEditBackground);
        DeleteObject(g_hFontNormal);
        DeleteObject(g_hFontBold);
        DeleteObject(g_hFontLog);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 18));
    wc.lpszClassName = "OmniGPU_GuestGUI";
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowExA(0, "OmniGPU_GuestGUI", "OmniGPU Guest Control",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 350, NULL, NULL, hInst, NULL);
    if (!hWnd) return 0;

    ShowWindow(hWnd, SW_SHOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
