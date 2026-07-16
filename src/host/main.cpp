#include "cli.h"
#include "config.h"
#include "server.h"
#include "service_manager.h"
#include "video_encoder.h"
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
              << "  " << prog << " --install               Install Windows service\n"
              << "  " << prog << " --uninstall             Uninstall Windows service\n"
              << "  " << prog << " --service               Run as Windows service\n"
              << "  " << prog << " --help                  Show this help\n";
}

static bool run_encoder_precheck(const omnigpu::HostConfig& config) {
    SPDLOG_INFO("Running encoder precheck...");
    auto encoder = omnigpu::create_best_encoder();
    if (!encoder) {
        SPDLOG_WARN("Precheck: No encoder available!");
        return false;
    }

    uint32_t w = 256;
    uint32_t h = 256;
    
    // Generate checkerboard pattern (RGBA)
    std::vector<uint8_t> checkerboard(w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool black = ((x / 32) + (y / 32)) % 2 == 0;
            uint8_t color = black ? 0 : 255;
            size_t idx = (y * w + x) * 4;
            checkerboard[idx + 0] = color;
            checkerboard[idx + 1] = color;
            checkerboard[idx + 2] = color;
            checkerboard[idx + 3] = 255;
        }
    }

    omnigpu::VideoCodec codec = omnigpu::codec_from_string(config.video_codec);
    if (!encoder->init(codec, w, h, 30, 4000)) {
        SPDLOG_WARN("Precheck: Failed to initialize encoder {}", encoder->name());
        return false;
    }

    std::vector<omnigpu::EncodedPacket> packets;
    if (!encoder->encode(checkerboard, packets)) {
        SPDLOG_WARN("Precheck: Failed to encode checkerboard frame using {}", encoder->name());
        encoder->shutdown();
        return false;
    }

    if (packets.empty()) {
        encoder->flush(packets);
    }

    if (packets.empty()) {
        SPDLOG_WARN("Precheck: Failed to get any encoded packets (even after flush) using {}", encoder->name());
        encoder->shutdown();
        return false;
    }

    SPDLOG_INFO("Precheck SUCCESS: Encoded checkerboard frame using {} (packet size = {} bytes)",
                 encoder->name(), packets[0].data.size());

    // Check if hardware acceleration is active
    bool is_hw = false;
    std::string name = encoder->name();
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (name.find("nvenc") != std::string::npos ||
        name.find("amf") != std::string::npos ||
        name.find("vaapi") != std::string::npos ||
        name.find("qsv") != std::string::npos) {
        is_hw = true;
    }

    if (!is_hw) {
        SPDLOG_WARN("==================================================================");
        SPDLOG_WARN("WARNING: FFmpeg hardware acceleration is NOT active or not found!");
        SPDLOG_WARN("Falling back to software encoding (this will have high CPU usage).");
        SPDLOG_WARN("Please ensure you have correct GPU drivers and FFmpeg HW components.");
        SPDLOG_WARN("==================================================================");
    } else {
        SPDLOG_INFO("FFmpeg Hardware Acceleration is active: {}", encoder->name());
    }

    encoder->shutdown();
    return true;
}

int main(int argc, char* argv[]) {
    bool debug = false;

    for (int i = 1; i < argc; i++) {
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
            if (omnigpu::run_as_service()) return 0;
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

    // Run encoder precheck on startup
    run_encoder_precheck(config);

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
