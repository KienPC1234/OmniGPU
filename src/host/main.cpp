#include "server.h"
#include "common/logger.h"
#include <cstdlib>
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    omnigpu::init_logger();

    uint16_t port = 9443;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    SPDLOG_INFO("OmniGPU Host starting on port {}...", port);

    omnigpu::Server server(port);
    if (!server.start()) {
        SPDLOG_CRITICAL("Failed to start OmniGPU Host server");
        return 1;
    }

    server.run();
    return 0;
}
