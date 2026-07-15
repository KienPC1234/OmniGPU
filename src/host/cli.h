#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace omnigpu {

class Server;

class HostCli {
public:
    explicit HostCli(Server& server);
    ~HostCli();

    void start();
    void stop();

private:
    Server& server_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void run();
    void handle_command(const std::string& line);
};

} // namespace omnigpu
