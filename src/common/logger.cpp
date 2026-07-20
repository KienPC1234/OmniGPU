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

static bool has_real_console() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        return false;
    }
    DWORD type = GetFileType(h);
    if (type != FILE_TYPE_CHAR) {
        return false;
    }
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

void init_logger(const char* log_name, bool debug) {
    std::vector<spdlog::sink_ptr> sinks;
    std::string log_path = log_name;

#ifdef _WIN32
    if (std::strcmp(log_name, "omnigpu_guest.log") == 0) {
        char exe_path[MAX_PATH] = {};
        bool app_dir_ok = false;
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) {
            std::string path_str(exe_path);
            size_t pos = path_str.find_last_of("\\/");
            if (pos != std::string::npos) {
                std::string app_dir_log = path_str.substr(0, pos + 1) + log_name;
                FILE* f = nullptr;
                fopen_s(&f, app_dir_log.c_str(), "a");
                if (f) {
                    fclose(f);
                    log_path = app_dir_log;
                    app_dir_ok = true;
                }
            }
        }
        if (!app_dir_ok) {
            char temp_path[MAX_PATH] = {};
            if (GetTempPathA(sizeof(temp_path), temp_path)) {
                log_path = std::string(temp_path) + log_name;
            }
        }
    } else {
        if (log_path.find('\\') == std::string::npos && log_path.find('/') == std::string::npos) {
            char temp_path[MAX_PATH] = {};
            if (GetTempPathA(sizeof(temp_path), temp_path)) {
                log_path = std::string(temp_path) + log_path;
            }
        }
    }
#endif

    // Primary: try to create log file alongside the executable or in temp
    bool file_sink_ok = false;
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            log_path, false);
        file_sink->set_level(spdlog::level::trace);
        sinks.push_back(file_sink);
        file_sink_ok = true;
    } catch (...) {}
    // Fallback: always try %TEMP% as well (works even in locked-down dirs)
    if (!file_sink_ok) {
        try {
            char temp_path[MAX_PATH] = {};
            if (GetTempPathA(sizeof(temp_path), temp_path)) {
                std::string fallback = std::string(temp_path) + log_name;
                auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    fallback, false);
                file_sink->set_level(spdlog::level::trace);
                sinks.push_back(file_sink);
            }
        } catch (...) {}
    }

    spdlog::level::level_enum console_level = debug ? spdlog::level::trace : spdlog::level::info;

    if (has_real_console()) {
        try {
            auto color_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            color_sink->set_level(console_level);
            sinks.push_back(color_sink);
        } catch (...) {
            try {
                auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
                plain_sink->set_level(console_level);
                sinks.push_back(plain_sink);
            } catch (...) {}
        }
    } else {
        try {
            auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
            plain_sink->set_level(console_level);
            sinks.push_back(plain_sink);
        } catch (...) {}
    }

    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>("omnigpu", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

} // namespace omnigpu
