#include "cli.h"
#include "config.h"
#include "server.h"
#include "service_manager.h"
#include "common/logger.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

static void print_usage(const char* prog) {
    std::cout << "OmniGPU Host v0.1.0\n"
              << "Usage:\n"
              << "  " << prog << "                         Run in CLI mode\n"
              << "  " << prog << " <config.json>          Run with config file\n"
              << "  " << prog << " --install               Install Windows service\n"
              << "  " << prog << " --uninstall             Uninstall Windows service\n"
              << "  " << prog << " --service               Run as Windows service\n"
              << "  " << prog << " --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (std::strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[1], "--install") == 0) {
            return omnigpu::install_service() ? 0 : 1;
        }
        if (std::strcmp(argv[1], "--uninstall") == 0) {
            return omnigpu::uninstall_service() ? 0 : 1;
        }
        if (std::strcmp(argv[1], "--service") == 0) {
            if (omnigpu::run_as_service()) {
                return 0; // Service dispatcher took over (Windows)
            }
        }
    }

    omnigpu::init_logger("omnigpu_host.log");

    omnigpu::HostConfig config;
    if (argc > 1) {
        config.config_path = argv[1];
    }
    omnigpu::load_config(config);

    SPDLOG_INFO("OmniGPU Host starting on port {}...", config.port);

    omnigpu::Server server(config);
    if (!server.start()) {
        SPDLOG_CRITICAL("Failed to start OmniGPU Host server");
        return 1;
    }

    omnigpu::HostCli cli(server);
    cli.start();

    server.run();

    cli.stop();
    return 0;
}
