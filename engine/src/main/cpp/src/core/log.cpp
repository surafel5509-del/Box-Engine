// Path: engine/src/main/cpp/src/core/log.cpp
#include "box/core/log.hpp"

#include <android/log.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace box::core {
namespace {

constexpr std::size_t kLogStreamCapacity = 256U;
constexpr std::size_t kMaximumLogMessageBytes = 1024U;
std::mutex logMutex;
std::deque<LogEntry> logEntries;
std::uint64_t nextLogSequence = 1U;

void appendLogEntry(int priority, const char* tag, const char* message) {
    std::lock_guard<std::mutex> lock(logMutex);
    logEntries.push_back({nextLogSequence++, priority, tag != nullptr ? tag : "BoxEngine",
                          message != nullptr ? message : ""});
    while (logEntries.size() > kLogStreamCapacity) logEntries.pop_front();
}

}  // namespace

void logPrint(int priority, const char* tag, const char* format, ...) {
    char message[kMaximumLogMessageBytes]{};
    va_list arguments;
    va_start(arguments, format);
    const int formatted = std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (formatted < 0) {
        message[0] = '\0';
    } else {
        message[sizeof(message) - 1U] = '\0';
    }

    const char* safeTag = tag != nullptr ? tag : "BoxEngine";
    (void)__android_log_write(priority, safeTag, message);
    appendLogEntry(priority, safeTag, message);
}

std::vector<LogEntry> recentLogEntries(std::size_t maximumEntries) {
    std::lock_guard<std::mutex> lock(logMutex);
    const std::size_t count = std::min(maximumEntries, logEntries.size());
    std::vector<LogEntry> result;
    result.reserve(count);
    const std::size_t first = logEntries.size() - count;
    for (std::size_t index = first; index < logEntries.size(); ++index) {
        result.push_back(logEntries[index]);
    }
    return result;
}

void clearLogStream() {
    std::lock_guard<std::mutex> lock(logMutex);
    logEntries.clear();
}

}  // namespace box::core
