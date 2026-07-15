// DLL entry point (Windows) and constructor (Linux)
// Initialization is deferred to ICD entrypoints (vk_icdGetInstanceProcAddr etc.)
// to avoid doing unsafe operations (network I/O, thread spawn) inside DllMain.

#include "icd_registry.h"
#include "guest_init.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInst);
        // Lightweight self-registration in HKCU so the ICD is discoverable
        // by future Vulkan apps without VK_ICD_FILENAMES. Registry API
        // calls are safe inside DllMain.
        if (!omnigpu::icd_registry::is_icd_registered()) {
            omnigpu::icd_registry::register_icd();
        }
        break;
    case DLL_PROCESS_DETACH:
        // Only shut down dynamically (on FreeLibrary).
        // On process termination (lpvReserved != nullptr), do NOT join threads
        // inside DllMain to avoid loader lock deadlock. The OS will automatically
        // clean up all sockets, threads, and memory anyway.
        if (lpvReserved == nullptr) {
            omnigpu::init::shutdown_guest();
        }
        break;
    }
    return TRUE;
}

#else
// Linux: use constructor/destructor attributes

__attribute__((constructor)) static void omnigpu_ctor() {
    // No init here — deferred to ICD entrypoints
}

__attribute__((destructor)) static void omnigpu_dtor() {
    omnigpu::init::shutdown_guest();
}

#endif
