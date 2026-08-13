/* ImWebBrowser - minimal logging facility. */

#include "logging/log.h"

#include <cstdio>
#include <ctime>
#include <atomic>
#include <mutex>

namespace imwb {

namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_mutex;

const char* level_name(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    }
    return "?";
}

} /* namespace */

void log_set_level(LogLevel level)
{
    g_level.store(level);
}

LogLevel log_get_level()
{
    return g_level.load();
}

void log_write(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    if (static_cast<int>(level) < static_cast<int>(g_level.load()))
        return;

    char time_buf[32] = {0};
    std::time_t now = std::time(nullptr);
    std::tm tm_buf;
    if (localtime_r(&now, &tm_buf))
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);

    std::lock_guard<std::mutex> lock(g_mutex);

    std::fprintf(stderr, "[%s] %-5s %s:%d: ", time_buf, level_name(level), file, line);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);

    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

} /* namespace imwb */
