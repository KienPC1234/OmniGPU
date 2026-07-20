#include "cli.h"
#include "config.h"
#include "server.h"
#include "session.h"

#include <cerrno>
#include <iostream>
#include <sstream>
#include <spdlog/spdlog.h>

#ifndef _WIN32
#include <poll.h>
#include <unistd.h>
#endif

namespace omnigpu {

HostCli::HostCli(Server& server) : server_(server) {}

HostCli::~HostCli() { stop(); }

void HostCli::start() {
    running_ = true;
    thread_ = std::thread(&HostCli::run, this);
    SPDLOG_INFO("CLI started - type 'help' for commands");
}

void HostCli::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HostCli::run() {
    std::string line;
    while (running_) {
#ifndef _WIN32
        pollfd input{};
        input.fd = STDIN_FILENO;
        input.events = POLLIN;
        const int ready = ::poll(&input, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) continue;
            SPDLOG_WARN("CLI poll failed: {}", errno);
            break;
        }
        if (ready == 0) continue;
        if ((input.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) break;
        if ((input.revents & POLLIN) == 0) continue;
#endif
        if (!std::getline(std::cin, line)) break;
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
        const auto gpu_count = server_.gpu_count();
        std::cout << "\nGPUs: " << gpu_count << "\n";
        for (int i = 0; i < gpu_count; ++i) {
            const auto info = server_.gpu_info(i);
            std::cout << "  GPU " << i << ": " << info.name
                      << " (sessions: " << info.sessionCount << ")\n";
        }

        const auto sessions = server_.session_summaries();
        std::cout << "Active Sessions: " << sessions.size() << "\n";
        for (const auto& session : sessions) {
            std::cout << "  Session #" << session.id
                      << ": GPU=" << session.gpu_index
                      << ", FPS=" << session.fps
                      << ", frames=" << session.frames_rendered << "\n";
        }
        std::cout << "\n";
    } else if (cmd == "sessions") {
        const auto sessions = server_.session_summaries();
        if (sessions.empty()) {
            std::cout << "No active sessions.\n";
        } else {
            std::cout << "Active Sessions (" << sessions.size() << "):\n";
            for (const auto& session : sessions) {
                std::cout << "  [" << session.id << "] GPU="
                          << session.gpu_index << ", FPS=" << session.fps
                          << ", frames=" << session.frames_rendered << "\n";
            }
        }
    } else if (cmd == "disconnect") {
        int id = 0;
        if (iss >> id) {
            std::cout << (server_.disconnect_session(id)
                              ? "Disconnected session "
                              : "Session not found: ")
                      << id << "\n";
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
