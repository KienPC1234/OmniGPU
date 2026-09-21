#include "logger.h"
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <vector>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define OMNIGPU_TRY try
#define OMNIGPU_CATCH_ALL catch (...)
#else
#define OMNIGPU_TRY if (true)
#define OMNIGPU_CATCH_ALL else if (false)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
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

// Portable "is this file writable" probe.
static bool probe_writable(const std::string& path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "a");
#else
    f = std::fopen(path.c_str(), "a");
#endif
    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

static std::string temp_dir() {
#ifdef _WIN32
    std::array<char, MAX_PATH> temp_path = {};
    if (GetTempPathA(static_cast<DWORD>(temp_path.size()), temp_path.data()) != 0) {
        return std::string(temp_path.data());
    }
#else
    const char* t = std::getenv("TMPDIR");
    if (t && *t) return std::string(t) + "/";
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/";
    return "/tmp/";
#endif
    return {};
}

void init_logger(const char* log_name, bool debug) {
    std::vector<spdlog::sink_ptr> sinks;
    std::string log_path = log_name;

#ifdef _WIN32
    if (std::strcmp(log_name, "omnigpu_guest.log") == 0) {
        std::array<char, MAX_PATH> exe_path = {};
        bool app_dir_ok = false;
        if (GetModuleFileNameA(nullptr, exe_path.data(), MAX_PATH) != 0) {
            std::string path_str(exe_path.data());
            size_t pos = path_str.find_last_of("\\/");
            if (pos != std::string::npos) {
                std::string app_dir_log = path_str.substr(0, pos + 1) + log_name;
                if (probe_writable(app_dir_log)) {
                    log_path = app_dir_log;
                    app_dir_ok = true;
                }
            }
        }
        if (!app_dir_ok) {
            log_path = temp_dir() + log_name;
        }
    } else if (log_path.find('\\') == std::string::npos &&
               log_path.find('/') == std::string::npos) {
        log_path = temp_dir() + log_path;
    }
#endif

    // Primary: try to create log file alongside the executable or in temp
    bool file_sink_ok = false;
    if (probe_writable(log_path)) {
        OMNIGPU_TRY {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                log_path, false);
            file_sink->set_level(spdlog::level::trace);
            sinks.push_back(file_sink);
            file_sink_ok = true;
        } OMNIGPU_CATCH_ALL {}
    }
    // Fallback: always try the temp directory as well (works in locked-down dirs)
    if (!file_sink_ok) {
        std::string fallback = temp_dir() + log_name;
        if (probe_writable(fallback)) {
            OMNIGPU_TRY {
                auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    fallback, false);
                file_sink->set_level(spdlog::level::trace);
                sinks.push_back(file_sink);
            } OMNIGPU_CATCH_ALL {}
        }
    }

    spdlog::level::level_enum console_level = debug ? spdlog::level::trace : spdlog::level::info;

    if (has_real_console()) {
        OMNIGPU_TRY {
            auto color_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            color_sink->set_level(console_level);
            sinks.push_back(color_sink);
        } OMNIGPU_CATCH_ALL {
            OMNIGPU_TRY {
                auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
                plain_sink->set_level(console_level);
                sinks.push_back(plain_sink);
            } OMNIGPU_CATCH_ALL {}
        }
    } else {
        OMNIGPU_TRY {
            auto plain_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
            plain_sink->set_level(console_level);
            sinks.push_back(plain_sink);
        } OMNIGPU_CATCH_ALL {}
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
