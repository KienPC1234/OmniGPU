#include "logger.h"
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace omnigpu {

// Check if stdout is a real interactive console (not a pipe/file/SSH pseudo-tty)
static bool has_real_console() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return false;
    DWORD type = GetFileType(h);
    if (type != FILE_TYPE_CHAR) return false; // It's a pipe or file, not a console
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

void init_logger(const char* log_name) {
    std::vector<spdlog::sink_ptr> sinks;
    std::string log_path = log_name;

#ifdef _WIN32
    if (log_path.find('\\') == std::string::npos && log_path.find('/') == std::string::npos) {
        char temp_path[MAX_PATH] = {};
        if (GetTempPathA(sizeof(temp_path), temp_path)) {
            log_path = std::string(temp_path) + log_path;
        }
    }
#endif

    // Always try to add a file sink for persistent logging
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            log_path, /*truncate=*/false);
        file_sink->set_level(spdlog::level::trace);
        sinks.push_back(file_sink);
    } catch (...) {
        // File logging unavailable (e.g., read-only directory), ignore
    }

    // Add a console sink only when we are in an interactive terminal
    // to avoid crash in color sink when stdout is a pipe (SSH, redirect, etc.)
    if (has_real_console()) {
        try {
            auto color_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            color_sink->set_level(spdlog::level::trace);
            sinks.push_back(color_sink);
        } catch (...) {
            // Fall back to plain stdout if color sink fails
            try {
                auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
                plain_sink->set_level(spdlog::level::trace);
                sinks.push_back(plain_sink);
            } catch (...) {}
        }
    } else {
        // Non-interactive: plain stdout (no ANSI codes, safe for pipes/SSH)
        try {
            auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
            plain_sink->set_level(spdlog::level::trace);
            sinks.push_back(plain_sink);
        } catch (...) {}
    }

    // Fallback if all sinks failed
    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>("omnigpu", sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%l] [%n] %v");
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::info); // Flush immediately on info+ for SSH readability
    spdlog::set_default_logger(logger);
}

} // namespace omnigpu
