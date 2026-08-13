/* ImWebBrowser - minimal logging facility (stderr, thread-safe). */

#ifndef IMWEBBROWSER_LOGGING_LOG_H
#define IMWEBBROWSER_LOGGING_LOG_H

#include <cstdarg>

namespace imwb {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

void log_set_level(LogLevel level);
LogLevel log_get_level();

void log_write(LogLevel level, const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

} /* namespace imwb */

#define LOG_TRACE(...) ::imwb::log_write(::imwb::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) ::imwb::log_write(::imwb::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) ::imwb::log_write(::imwb::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) ::imwb::log_write(::imwb::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) ::imwb::log_write(::imwb::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

#endif /* IMWEBBROWSER_LOGGING_LOG_H */
