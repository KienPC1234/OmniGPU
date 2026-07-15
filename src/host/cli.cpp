#include "cli.h"
#include "config.h"
#include "server.h"
#include "session.h"
#include <iostream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace omnigpu {

HostCli::HostCli(Server& server) : server_(server) {}

HostCli::~HostCli() { stop(); }

void HostCli::start() {
    running_ = true;
    thread_ = std::thread(&HostCli::run, this);
    SPDLOG_INFO("CLI started — type 'help' for commands");
}

void HostCli::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HostCli::run() {
    std::string line;
    while (running_) {
        if (!std::getline(std::cin, line)) {
            break;
        }
        handle_command(line);
    }
}

void HostCli::handle_command(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "help") {
        std::cout << "\n=== OmniGPU Host CLI ===\n";
        std::cout << "  status              — Show GPUs & sessions\n";
        std::cout << "  sessions            — List all sessions\n";
        std::cout << "  disconnect <id>     — Force-disconnect a session\n";
        std::cout << "  config              — Show current config\n";
        std::cout << "  help                — This help\n";
        std::cout << "  quit                — Stop server\n";
        std::cout << "========================\n\n";
    } else if (cmd == "status") {
        auto gpuCount = server_.gpu_count();
        std::cout << "\nGPUs: " << gpuCount << "\n";
        for (int i = 0; i < gpuCount; ++i) {
            auto info = server_.gpu_info(i);
            std::cout << "  GPU " << i << ": " << info.name
                      << " (sessions: " << info.sessionCount << ")\n";
        }

        auto sessions = server_.session_summaries();
        std::cout << "Active Sessions: " << sessions.size() << "\n";
        for (auto& s : sessions) {
            std::cout << "  Session #" << s.id << ": GPU=" << s.gpu_index
                      << ", FPS=" << s.fps
                      << ", frames=" << s.frames_rendered << "\n";
        }
        std::cout << "\n";
    } else if (cmd == "sessions") {
        auto sessions = server_.session_summaries();
        if (sessions.empty()) {
            std::cout << "No active sessions.\n";
        } else {
            std::cout << "Active Sessions (" << sessions.size() << "):\n";
            for (auto& s : sessions) {
                std::cout << "  [" << s.id << "] GPU=" << s.gpu_index
                          << ", FPS=" << s.fps
                          << ", frames=" << s.frames_rendered << "\n";
            }
        }
    } else if (cmd == "disconnect") {
        int id;
        if (iss >> id) {
            if (server_.disconnect_session(id)) {
                std::cout << "Disconnected session " << id << "\n";
            } else {
                std::cout << "Session " << id << " not found\n";
            }
        } else {
            std::cout << "Usage: disconnect <session_id>\n";
        }
    } else if (cmd == "config") {
        print_config(server_.config());
    } else if (cmd == "quit") {
        std::cout << "Stopping server...\n";
        server_.stop();
    } else if (!cmd.empty()) {
        std::cout << "Unknown command: '" << cmd << "'. Type 'help'.\n";
    }
}

} // namespace omnigpu
