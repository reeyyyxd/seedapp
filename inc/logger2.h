// #ifndef LOGGER_H
// #define LOGGER_H

// #include <stdio.h>
// #include <errno.h>
// #include <string.h>
// #include <time.h>

// static inline void log_timestamp(char* out, size_t cap) {
//     time_t t = time(NULL);
//     struct tm tmv;
// #if defined(_WIN32)

//     localtime_s(&tmv, &t);
// #else

//     localtime_r(&t, &tmv);
// #endif

//     strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
// }

// #ifdef LOG_TO_FILE
// #define log(lvl, fmt, ...) do { \
//     FILE* fp = fopen("log.txt", "a"); \
//     if (fp) { \
//         char ts[20]; \
//         log_timestamp(ts, sizeof(ts)); \
//         fprintf(fp, "%s %s %s(%d) " fmt "\n", lvl, ts, __func__, __LINE__, ##__VA_ARGS__); \
//         fclose(fp); \
//     } \
// } while(0)
// #else
// #define log(lvl, fmt, ...) do { \
//     char ts[20]; \
//     log_timestamp(ts, sizeof(ts)); \
//     printf("%s %s %s(%d) " fmt "\n", lvl, ts, __func__, __LINE__, ##__VA_ARGS__); \
// } while(0)
// #endif

// #define logDbg(fmt, ...)  log("[Debug]", fmt, ##__VA_ARGS__)
// #define logInfo(fmt, ...) log("[Info ]", fmt, ##__VA_ARGS__)
// #define logWarn(fmt, ...) log("[Warn ]", fmt, ##__VA_ARGS__)
// #define logErr(fmt, ...)  log("[Error]", fmt, ##__VA_ARGS__)

// #endif

#ifndef LOGGER_H
#define LOGGER_H

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