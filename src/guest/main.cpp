// Standalone guest executable entry point.
// When built as a shared library (DLL/SO), initialization
// happens automatically via DllMain / constructor in dll_main.cpp.
// This main() is used for debugging and testing only.

#include "guest_init.h"
#include "common/logger.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstdlib>
#include <thread>

int main(int argc, char* argv[]) {
    omnigpu::init_logger();

    const char* host = nullptr;
    uint16_t port = 0;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::atoi(argv[2]));

    if (!omnigpu::init::initialize_guest(host, port)) {
        SPDLOG_CRITICAL("OmniGPU Guest initialization failed");
        return 1;
    }

    // Perform connection to host for testing
    omnigpu::init::connect_to_host();

    SPDLOG_INFO("Guest running. Press Ctrl+C to stop.");

    // Keep alive
    std::this_thread::sleep_for(std::chrono::hours(24));

    omnigpu::init::shutdown_guest();
    return 0;
}
