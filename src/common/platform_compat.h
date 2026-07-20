#pragma once

// Small compatibility bridge for legacy Microsoft secure-CRT calls that are
// still present in the codebase. New code should prefer the C++ standard
// library directly. This header is force-included only for non-Windows C++
// compilation units by the top-level CMake configuration.
#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits.h>
#include <unistd.h>

using errno_t = int;
using DWORD = std::uint32_t;

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif

#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<std::size_t>(-1))
#endif

inline errno_t fopen_s(FILE** stream, const char* filename, const char* mode) {
    if (stream == nullptr || filename == nullptr || mode == nullptr) {
        if (stream != nullptr) *stream = nullptr;
        return EINVAL;
    }
    *stream = std::fopen(filename, mode);
    return *stream != nullptr ? 0 : errno;
}

inline errno_t memcpy_s(void* destination, std::size_t destination_size,
                        const void* source, std::size_t count) {
    if (destination == nullptr || source == nullptr || count > destination_size) {
        if (destination != nullptr && destination_size != 0) {
            std::memset(destination, 0, destination_size);
        }
        return EINVAL;
    }
    std::memcpy(destination, source, count);
    return 0;
}

inline errno_t strcpy_s(char* destination, std::size_t destination_size,
                        const char* source) {
    if (destination == nullptr || destination_size == 0 || source == nullptr) {
        if (destination != nullptr && destination_size != 0) destination[0] = '\0';
        return EINVAL;
    }
    const std::size_t source_size = std::strlen(source) + 1;
    if (source_size > destination_size) {
        destination[0] = '\0';
        return ERANGE;
    }
    std::memcpy(destination, source, source_size);
    return 0;
}

template <std::size_t Size>
inline errno_t strcpy_s(char (&destination)[Size], const char* source) {
    return strcpy_s(destination, Size, source);
}

inline errno_t strncpy_s(char* destination, std::size_t destination_size,
                         const char* source, std::size_t count) {
    if (destination == nullptr || destination_size == 0 || source == nullptr) {
        if (destination != nullptr && destination_size != 0) destination[0] = '\0';
        return EINVAL;
    }

    const std::size_t source_length = std::strlen(source);
    const std::size_t requested = count == _TRUNCATE
        ? source_length
        : std::min(source_length, count);
    const std::size_t copied = std::min(requested, destination_size - 1);

    if (copied != 0) std::memcpy(destination, source, copied);
    destination[copied] = '\0';

    if (requested >= destination_size && count != _TRUNCATE) return ERANGE;
    return 0;
}

template <std::size_t Size>
inline errno_t strncpy_s(char (&destination)[Size], const char* source,
                         std::size_t count) {
    return strncpy_s(destination, Size, source, count);
}

inline errno_t strcat_s(char* destination, std::size_t destination_size,
                        const char* source) {
    if (destination == nullptr || source == nullptr || destination_size == 0) {
        return EINVAL;
    }
    const std::size_t existing = strnlen(destination, destination_size);
    if (existing == destination_size) return EINVAL;
    return strcpy_s(destination + existing, destination_size - existing, source);
}

template <std::size_t Size>
inline errno_t strcat_s(char (&destination)[Size], const char* source) {
    return strcat_s(destination, Size, source);
}

inline int sprintf_s(char* destination, std::size_t destination_size,
                     const char* format, ...) {
    if (destination == nullptr || destination_size == 0 || format == nullptr) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    const int result = std::vsnprintf(destination, destination_size, format, args);
    va_end(args);
    if (result < 0 || static_cast<std::size_t>(result) >= destination_size) {
        destination[0] = '\0';
        return -1;
    }
    return result;
}

inline errno_t localtime_s(std::tm* output, const std::time_t* input) {
    if (output == nullptr || input == nullptr) return EINVAL;
    return localtime_r(input, output) != nullptr ? 0 : errno;
}

inline errno_t gmtime_s(std::tm* output, const std::time_t* input) {
    if (output == nullptr || input == nullptr) return EINVAL;
    return gmtime_r(input, output) != nullptr ? 0 : errno;
}

// Win32-compatible helper used by the logger fallback path.
inline DWORD GetTempPathA(DWORD buffer_length, char* buffer) {
    const char* configured = std::getenv("TMPDIR");
    const char* base = configured != nullptr && configured[0] != '\0'
        ? configured
        : "/tmp";

    const std::size_t base_length = std::strlen(base);
    const bool has_separator = base_length != 0 && base[base_length - 1] == '/';
    const std::size_t required = base_length + (has_separator ? 0 : 1);
    if (buffer == nullptr || buffer_length <= required) {
        return static_cast<DWORD>(required + 1);
    }

    std::memcpy(buffer, base, base_length);
    std::size_t offset = base_length;
    if (!has_separator) buffer[offset++] = '/';
    buffer[offset] = '\0';
    return static_cast<DWORD>(offset);
}

#endif // !_WIN32
