#ifndef LOGGER2_H
#define LOGGER2_H

#include <cstdarg>

class Logger {
public:
    enum Level { TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4 };

    // Call once at startup
    static void init(const char* appName,
                     Level consoleLevel = INFO,
                     Level fileLevel = TRACE,
                     const char* logDir = "logs");

    // Optional runtime context (useful for file name)
    static void setPort(int port);

    // Write buffered logs to a single .txt file (safe to call multiple times)
    static void flushToFile();

    // Logging entry point (printf-style)
    static void log(Level lvl,
                    const char* component,
                    const char* file,
                    int line,
                    const char* func,
                    const char* fmt, ...);

    static void vlog(Level lvl,
                     const char* component,
                     const char* file,
                     int line,
                     const char* func,
                     const char* fmt,
                     va_list ap);

private:
    Logger();
};

// Component-tagged macros (recommended for client/server/net)
#define LWLOG_TRACE(component, fmt, ...) Logger::log(Logger::TRACE, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_DEBUG(component, fmt, ...) Logger::log(Logger::DEBUG, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_INFO(component, fmt, ...)  Logger::log(Logger::INFO,  component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_WARN(component, fmt, ...)  Logger::log(Logger::WARN,  component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_ERROR(component, fmt, ...) Logger::log(Logger::ERROR, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
// Backwards-compatible macros (your existing code can keep using these)
#define logTrace(fmt, ...) LWLOG_TRACE("APP", fmt, ##__VA_ARGS__)
#define logDbg(fmt, ...)   LWLOG_DEBUG("APP", fmt, ##__VA_ARGS__)
#define logInfo(fmt, ...)  LWLOG_INFO("APP", fmt, ##__VA_ARGS__)
#define logWarn(fmt, ...)  LWLOG_WARN("APP", fmt, ##__VA_ARGS__)
#define logErr(fmt, ...)   LWLOG_ERROR("APP", fmt, ##__VA_ARGS__)

#endif