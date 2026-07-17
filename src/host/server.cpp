#include "server.h"
#include "session.h"
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

namespace omnigpu {

Server::Server(const HostConfig& config) : config_(config), port_(config.port) {}

Server::~Server() { stop(); }

bool Server::start() {
    SPDLOG_INFO("Server initializing on port {}...", port_);

    if (!tcp::init()) {
        return false;
    }

    if (!gpuMgr_.init()) {
        SPDLOG_WARN("No Vulkan GPUs available, running in headless mode");
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ == INVALID_SOCKET) {
        SPDLOG_ERROR("socket creation failed: {}", tcp::last_error());
        return false;
    }

    int opt = 1;
    if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        SPDLOG_WARN("SO_REUSEADDR failed: {}", tcp::last_error());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        SPDLOG_ERROR("bind failed on port {}: {}", port_, tcp::last_error());
        tcp::close_socket(listenFd_);
        listenFd_ = INVALID_SOCKET;
        return false;
    }

    if (::listen(listenFd_, SOMAXCONN) != 0) {
        SPDLOG_ERROR("listen failed: {}", tcp::last_error());
        tcp::close_socket(listenFd_);
        listenFd_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    print_config(config_);
    SPDLOG_INFO("Server listening on port {} ({} GPU(s))",
                port_, gpuMgr_.gpu_count());
    return true;
}

void Server::run() {
    SPDLOG_INFO("Server entering main loop");

    while (running_) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd_, &readSet);

        struct timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        int ret = ::select(static_cast<int>(listenFd_ + 1), &readSet,
                           nullptr, nullptr, &timeout);

        if (ret == SOCKET_ERROR) {
            if (running_) {
                SPDLOG_ERROR("select failed: {}", tcp::last_error());
            }
            continue;
        }

        // Check for new connection
        if (ret > 0 && FD_ISSET(listenFd_, &readSet)) {
            sockaddr_in clientAddr{};
#ifdef _WIN32
            int addrLen = sizeof(clientAddr);
#else
            socklen_t addrLen = sizeof(clientAddr);
#endif
            SOCKET clientFd =
                ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr),
                         &addrLen);

            if (clientFd == INVALID_SOCKET) {
                if (running_) {
                    SPDLOG_ERROR("accept failed: {}", tcp::last_error());
                }
                continue;
            }

            char clientIp[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
            uint16_t clientPort = ntohs(clientAddr.sin_port);

            // --- Security: rate limiting ---
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(rateMutex_);
                // Clean old entries
                while (!connTimestamps_.empty() &&
                       std::chrono::duration_cast<std::chrono::seconds>(
                           now - connTimestamps_.front()).count() > 1) {
                    connTimestamps_.pop_front();
                }
                // Max 10 connections per second
                if (connTimestamps_.size() >= 10) {
                    SPDLOG_WARN("SECURITY: Rate limit exceeded from {}:{}, rejecting",
                                clientIp, clientPort);
                    tcp::close_socket(clientFd);
                    continue;
                }
                connTimestamps_.push_back(now);
            }

            // --- Security: max sessions ---
            {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                if (sessions_.size() >= config_.max_sessions) {
                    SPDLOG_WARN("SECURITY: Max sessions ({}) reached, rejecting {}:{}",
                                config_.max_sessions, clientIp, clientPort);
                    tcp::close_socket(clientFd);
                    continue;
                }
            }

            SPDLOG_INFO("Accepted connection from {}:{} (session #{})",
                        clientIp, clientPort, nextSessionId_);

            tcp::set_tcp_nodelay(clientFd);
            tcp::set_tcp_timeout(clientFd, config_.session_timeout_s);

            int sessionId = nextSessionId_++;

            int teamSize = (gpuMgr_.gpu_count() >= 2) ? 2 : 1;
            auto gpuIndices = gpuMgr_.acquire_gpu_team(teamSize);

            auto session = std::make_unique<Session>(
                clientFd, gpuMgr_, gpuIndices, sessionId, config_);
            session->start();

            {
                std::lock_guard<std::mutex> lock(sessionsMutex_);
                sessions_.push_back(std::move(session));
            }
        }

        cleanup_stopped_sessions();
    }

    // Stop all remaining sessions
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto& s : sessions_) {
            if (s) s->stop();
        }
        sessions_.clear();
    }

    // Close listen socket after loop exits
    if (listenFd_ != INVALID_SOCKET) {
        tcp::close_socket(listenFd_);
        listenFd_ = INVALID_SOCKET;
    }

    tcp::cleanup();
    SPDLOG_INFO("Server stopped");
}

void Server::stop() {
    running_ = false;
    SPDLOG_INFO("Server stopping... (waiting for accept loop)");
}

void Server::cleanup_stopped_sessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (!(*it)->is_running()) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

int Server::gpu_count() const {
    return gpuMgr_.gpu_count();
}

GpuInfo Server::gpu_info(int index) const {
    return gpuMgr_.gpu_info(index);
}

std::vector<SessionSummary> Server::session_summaries() const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    std::vector<SessionSummary> summaries;
    for (auto& s : sessions_) {
        if (s && s->is_running()) {
            summaries.push_back(s->summary());
        }
    }
    return summaries;
}

bool Server::disconnect_session(int sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto& s : sessions_) {
        if (s && s->is_running()) {
            auto sum = s->summary();
            if (sum.id == sessionId) {
                s->stop();
                return true;
            }
        }
    }
    return false;
}

} // namespace omnigpu
