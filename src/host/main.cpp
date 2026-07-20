#include "cli.h"
#include "config.h"
#include "server.h"
#include "service_manager.h"
#include "video_encoder.h"
#include "common/logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void request_stop(int) {
    g_stop_requested = 1;
}

bool stdin_is_interactive() {
#ifdef _WIN32
    return true;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

void print_usage(const char* prog) {
    std::cout << "OmniGPU Host v0.1.0\n"
              << "Usage:\n"
              << "  " << prog << "                         Run in CLI mode\n"
              << "  " << prog << " <config.json>          Run with config file\n"
              << "  " << prog << " --debug                 Run with verbose console logging\n"
              << "  " << prog << " --no-cli                Disable interactive stdin CLI\n"
              << "  " << prog << " --install               Install service\n"
              << "  " << prog << " --uninstall             Uninstall service\n"
              << "  " << prog << " --service               Run as service\n"
              << "  " << prog << " --help                  Show this help\n";
}

bool run_encoder_precheck(const omnigpu::HostConfig& config) {
    SPDLOG_INFO("Running encoder precheck...");
    auto encoder = omnigpu::create_best_encoder();
    if (!encoder) {
        SPDLOG_WARN("Precheck: no encoder available; compute forwarding remains usable");
        return false;
    }

    constexpr uint32_t width = 256;
    constexpr uint32_t height = 256;
    std::vector<uint8_t> checkerboard(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool black = ((x / 32) + (y / 32)) % 2 == 0;
            const uint8_t color = black ? 0 : 255;
            const size_t idx = (y * width + x) * 4;
            checkerboard[idx + 0] = color;
            checkerboard[idx + 1] = color;
            checkerboard[idx + 2] = color;
            checkerboard[idx + 3] = 255;
        }
    }

    const omnigpu::VideoCodec codec =
        omnigpu::codec_from_string(config.video_codec);
    if (!encoder->init(codec, width, height, 30, 4000)) {
        SPDLOG_WARN("Precheck: failed to initialize encoder {}",
                    encoder->name());
        return false;
    }

    std::vector<omnigpu::EncodedPacket> packets;
    if (!encoder->encode(checkerboard, packets)) {
        SPDLOG_WARN("Precheck: failed to encode checkerboard using {}",
                    encoder->name());
        encoder->shutdown();
        return false;
    }
    if (packets.empty()) encoder->flush(packets);
    if (packets.empty()) {
        SPDLOG_WARN("Precheck: encoder {} produced no packets", encoder->name());
        encoder->shutdown();
        return false;
    }

    SPDLOG_INFO("Precheck SUCCESS: {} produced {} bytes",
                encoder->name(), packets.front().data.size());

    std::string name = encoder->name();
    for (auto& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const bool hardware = name.find("nvenc") != std::string::npos ||
                          name.find("amf") != std::string::npos ||
                          name.find("vaapi") != std::string::npos ||
                          name.find("qsv") != std::string::npos;
    if (!hardware) {
        SPDLOG_WARN("FFmpeg hardware acceleration is not active; rendering will use software encoding");
    } else {
        SPDLOG_INFO("Hardware acceleration active: {}", encoder->name());
    }

    encoder->shutdown();
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    bool debug = false;
    bool no_cli = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--install") == 0) {
            return omnigpu::install_service() ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--uninstall") == 0) {
            return omnigpu::uninstall_service() ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--service") == 0) {
            no_cli = true;
            if (omnigpu::run_as_service()) return 0;
        }
        if (std::strcmp(argv[i], "--debug") == 0) debug = true;
        if (std::strcmp(argv[i], "--no-cli") == 0) no_cli = true;
    }

    omnigpu::init_logger("omnigpu_host.log", debug);

    omnigpu::HostConfig config;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            config.config_path = argv[i];
            break;
        }
    }
    omnigpu::load_config(config);

    // Rendering is secondary; an encoder failure must not prevent compute use.
    run_encoder_precheck(config);

    SPDLOG_INFO("OmniGPU Host starting on port {}...", config.port);
    if (debug) SPDLOG_INFO("Debug mode enabled");

    omnigpu::Server server(config);
    if (!server.start()) {
        SPDLOG_CRITICAL("Failed to start OmniGPU Host server");
        return 1;
    }

    std::signal(SIGINT, request_stop);
#ifdef SIGTERM
    std::signal(SIGTERM, request_stop);
#endif

    std::atomic<bool> monitor_running{true};
    std::thread signal_monitor([&]() {
        while (monitor_running.load(std::memory_order_acquire)) {
            if (g_stop_requested != 0) {
                SPDLOG_INFO("Termination signal received");
                server.stop();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    const bool cli_enabled = !no_cli && stdin_is_interactive();
    omnigpu::HostCli cli(server);
    if (cli_enabled) {
        cli.start();
    } else {
        SPDLOG_INFO("Interactive CLI disabled (daemon/non-TTY mode)");
    }

    server.run();

    monitor_running.store(false, std::memory_order_release);
    if (signal_monitor.joinable()) signal_monitor.join();
    if (cli_enabled) cli.stop();
    return 0;
}
