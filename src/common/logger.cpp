#include "logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

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
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace omnigpu {
namespace {

namespace fs = std::filesystem;

bool has_real_console() {
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }
    if (GetFileType(handle) != FILE_TYPE_CHAR) {
        return false;
    }
    DWORD mode = 0;
    return GetConsoleMode(handle, &mode) != 0;
#else
    return ::isatty(::fileno(stdout)) != 0;
#endif
}

bool has_path_separator(const std::string& value) {
    return value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos;
}

#ifndef _WIN32
std::optional<fs::path> linux_log_path(const std::string& log_name) {
    if (has_path_separator(log_name)) {
        return fs::path(log_name);
    }

    if (const char* state_home = std::getenv("XDG_STATE_HOME")) {
        if (state_home[0] != '\0') {
            return fs::path(state_home) / "omnigpu" / log_name;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (home[0] != '\0') {
            return fs::path(home) / ".local" / "state" / "omnigpu" /
                   log_name;
        }
    }
    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
        if (cache_home[0] != '\0') {
            return fs::path(cache_home) / "omnigpu" / log_name;
        }
    }

    // A shared predictable /tmp filename is unsafe for a library loaded into
    // arbitrary user processes. If there is no private per-user directory,
    // retain console logging and omit the file sink.
    return std::nullopt;
}
#endif

std::vector<fs::path> candidate_log_paths(const std::string& log_name) {
    std::vector<fs::path> candidates;
#ifdef _WIN32
    if (has_path_separator(log_name)) {
        candidates.emplace_back(log_name);
        return candidates;
    }

    if (log_name == "omnigpu_guest.log") {
        std::array<char, MAX_PATH> executable{};
        if (GetModuleFileNameA(nullptr, executable.data(),
                               static_cast<DWORD>(executable.size())) != 0) {
            candidates.push_back(fs::path(executable.data()).parent_path() /
                                 log_name);
        }
    }

    std::array<char, MAX_PATH> temporary{};
    if (GetTempPathA(static_cast<DWORD>(temporary.size()), temporary.data()) !=
        0) {
        candidates.push_back(fs::path(temporary.data()) / log_name);
    }
    if (candidates.empty()) candidates.emplace_back(log_name);
#else
    if (const auto selected = linux_log_path(log_name)) {
        candidates.push_back(*selected);
    }
#endif
    return candidates;
}

bool prepare_log_file(const fs::path& path) {
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) {
            return false;
        }
#ifndef _WIN32
        // The selected Linux directory is dedicated to OmniGPU. Keep logs and
        // any future state inaccessible to other local users.
        ::chmod(parent.c_str(), S_IRWXU);
#endif
    }

    FILE* file = std::fopen(path.string().c_str(), "a");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
#ifndef _WIN32
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
}

}  // namespace

void init_logger(const char* log_name, bool debug) {
    const std::string requested =
        (log_name != nullptr && log_name[0] != '\0') ? log_name
                                                     : "omnigpu.log";
    std::vector<spdlog::sink_ptr> sinks;

    for (const auto& log_path : candidate_log_paths(requested)) {
        if (!prepare_log_file(log_path)) continue;
        bool installed = false;
        OMNIGPU_TRY {
            auto file_sink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    log_path.string(), false);
            file_sink->set_level(spdlog::level::trace);
            sinks.push_back(file_sink);
            installed = true;
        }
        OMNIGPU_CATCH_ALL {
        }
        if (installed) break;
    }

    const auto console_level =
        debug ? spdlog::level::trace : spdlog::level::info;
    if (has_real_console()) {
        OMNIGPU_TRY {
            auto color_sink =
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            color_sink->set_level(console_level);
            sinks.push_back(color_sink);
        }
        OMNIGPU_CATCH_ALL {
            OMNIGPU_TRY {
                auto plain_sink =
                    std::make_shared<spdlog::sinks::stdout_sink_mt>();
                plain_sink->set_level(console_level);
                sinks.push_back(plain_sink);
            }
            OMNIGPU_CATCH_ALL {
            }
        }
    } else {
        OMNIGPU_TRY {
            auto plain_sink =
                std::make_shared<spdlog::sinks::stdout_sink_mt>();
            plain_sink->set_level(console_level);
            sinks.push_back(plain_sink);
        }
        OMNIGPU_CATCH_ALL {
        }
    }

    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>(
        "omnigpu", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

}  // namespace omnigpu
