#include "cli.h"
#include "config.h"
#include "server.h"
#include "common/logger.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>

static void print_usage(const char* prog) {
    std::cout << "OmniGPU Host v0.1.0\n"
              << "Usage:\n"
              << "  " << prog << "                         Run in CLI mode\n"
              << "  " << prog << " <config.json>          Run with config file\n"
              << "  " << prog << " --debug                 Run with verbose console logging\n"
              << "  " << prog << " --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    bool debug = false;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }

    omnigpu::init_logger("omnigpu_host.log", debug);

    omnigpu::HostConfig config;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            config.config_path = argv[i];
            break;
        }
    }
    omnigpu::load_config(config);

    SPDLOG_INFO("OmniGPU Host starting on port {}...", config.port);
    if (debug) SPDLOG_INFO("Debug mode enabled - verbose console logging");

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
