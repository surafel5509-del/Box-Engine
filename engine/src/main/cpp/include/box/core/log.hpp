// Path: engine/src/main/cpp/include/box/core/log.hpp
#pragma once

#include <android/log.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef BOX_ENGINE_LOG_TAG
#define BOX_ENGINE_LOG_TAG "BoxEngine"
#endif

namespace box::core {

struct LogEntry {
    std::uint64_t sequence = 0U;
    int priority = ANDROID_LOG_INFO;
    std::string tag;
    std::string message;
};

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void logPrint(int priority, const char* tag, const char* format, ...);

[[nodiscard]] std::vector<LogEntry> recentLogEntries(std::size_t maximumEntries = 32U);
void clearLogStream();

}  // namespace box::core

#define BOX_LOG_VERBOSE(...) \
    ::box::core::logPrint(ANDROID_LOG_VERBOSE, BOX_ENGINE_LOG_TAG, __VA_ARGS__)
#define BOX_LOG_DEBUG(...) \
    ::box::core::logPrint(ANDROID_LOG_DEBUG, BOX_ENGINE_LOG_TAG, __VA_ARGS__)
#define BOX_LOG_INFO(...) \
    ::box::core::logPrint(ANDROID_LOG_INFO, BOX_ENGINE_LOG_TAG, __VA_ARGS__)
#define BOX_LOG_WARN(...) \
    ::box::core::logPrint(ANDROID_LOG_WARN, BOX_ENGINE_LOG_TAG, __VA_ARGS__)
#define BOX_LOG_ERROR(...) \
    ::box::core::logPrint(ANDROID_LOG_ERROR, BOX_ENGINE_LOG_TAG, __VA_ARGS__)
