#ifndef LOGGER_H
#define LOGGER_H

#include <cstdarg>

class Logger {
public:
    enum Level { TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4 };

    // Call once at startup (safe to call multiple times)
    static void init(const char* appName = "SeedApp",
                     Level consoleLevel = INFO,
                     Level fileLevel = TRACE,
                     const char* logDir = "."); 

    static void setPort(int port);
    static void flushToFile();

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

// Component-tagged macros (optional)
#define LWLOG_TRACE(component, fmt, ...) Logger::log(Logger::TRACE, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_DEBUG(component, fmt, ...) Logger::log(Logger::DEBUG, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_INFO(component, fmt, ...)  Logger::log(Logger::INFO,  component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_WARN(component, fmt, ...)  Logger::log(Logger::WARN,  component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LWLOG_ERROR(component, fmt, ...) Logger::log(Logger::ERROR, component, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// KEEP THESE (your classes already use them)
#define logTrace(fmt, ...) LWLOG_TRACE("APP", fmt, ##__VA_ARGS__)
#define logDbg(fmt, ...)   LWLOG_DEBUG("APP", fmt, ##__VA_ARGS__)
#define logInfo(fmt, ...)  LWLOG_INFO("APP", fmt, ##__VA_ARGS__)
#define logWarn(fmt, ...)  LWLOG_WARN("APP", fmt, ##__VA_ARGS__)
#define logErr(fmt, ...)   LWLOG_ERROR("APP", fmt, ##__VA_ARGS__)



#define cTrace(fmt, ...) LWLOG_TRACE("CLIENT", fmt, ##__VA_ARGS__)
#define cDbg(fmt, ...)   LWLOG_DEBUG("CLIENT", fmt, ##__VA_ARGS__)
#define cInfo(fmt, ...)  LWLOG_INFO("CLIENT",  fmt, ##__VA_ARGS__)
#define cWarn(fmt, ...)  LWLOG_WARN("CLIENT",  fmt, ##__VA_ARGS__)
#define cErr(fmt, ...)   LWLOG_ERROR("CLIENT", fmt, ##__VA_ARGS__)

#define sTrace(fmt, ...) LWLOG_TRACE("SERVER", fmt, ##__VA_ARGS__)
#define sDbg(fmt, ...)   LWLOG_DEBUG("SERVER", fmt, ##__VA_ARGS__)
#define sInfo(fmt, ...)  LWLOG_INFO("SERVER",  fmt, ##__VA_ARGS__)
#define sWarn(fmt, ...)  LWLOG_WARN("SERVER",  fmt, ##__VA_ARGS__)
#define sErr(fmt, ...)   LWLOG_ERROR("SERVER", fmt, ##__VA_ARGS__)
#endif