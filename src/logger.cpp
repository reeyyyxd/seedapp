#include "../inc/logger2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <deque>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <csignal>
#include <exception>

struct LoggerState {
    bool initialized;
    bool flushed;

    std::string appName;
    std::string logDir;
    int port;

    Logger::Level consoleLevel;
    Logger::Level fileLevel;

    std::string startStamp;

    std::mutex mu;
    std::deque<std::string> lines;
    size_t maxLines;

    LoggerState()
        : initialized(false),
          flushed(false),
          appName("SeedApp"),
          logDir("."),
          port(-1),
          consoleLevel(Logger::INFO),
          fileLevel(Logger::TRACE),
          startStamp(""),
          maxLines(50000) {}
};

static LoggerState& LS() {
    static LoggerState st;
    return st;
}

static const char* levelName(Logger::Level lvl) {
    switch (lvl) {
        case Logger::TRACE: return "TRACE";
        case Logger::DEBUG: return "DEBUG";
        case Logger::INFO:  return "INFO";
        case Logger::WARN:  return "WARN";
        case Logger::ERROR: return "ERROR";
        default:            return "INFO";
    }
}

static std::string baseName(const char* path) {
    if (!path) return "";
    const char* s1 = std::strrchr(path, '/');
    const char* s2 = std::strrchr(path, '\\');
    const char* p = s1;
    if (s2 && (!p || s2 > p)) p = s2;
    return p ? (p + 1) : path;
}

// std::localtime is not thread-safe -> guard it
static void localTimeSafe(std::tm& out, std::time_t t) {
    static std::mutex tmMu;
    std::lock_guard<std::mutex> lock(tmMu);
    std::tm* p = std::localtime(&t);
    if (p) out = *p;
    else std::memset(&out, 0, sizeof(out));
}

static std::string stampNowYMD_HMS(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmv;
    localTimeSafe(tmv, t);
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y%m%d_%H%M%S");
    return oss.str();
}

static std::string timestampMs() {
    using namespace std::chrono;
    system_clock::time_point now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tmv;
    localTimeSafe(tmv, t);

    milliseconds ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

// Flush hooks
static void flushAtExit() {
    Logger::flushToFile();
}

static void onSignal(int /*sig*/) {
    Logger::flushToFile();
    std::exit(1);
}

static void onTerminate() {
    Logger::flushToFile();
    std::abort();
}

Logger::Logger() {}

void Logger::init(const char* appName, Level consoleLevel, Level fileLevel, const char* logDir) {
    LoggerState& st = LS();
    std::lock_guard<std::mutex> lock(st.mu);

    if (st.initialized) return;

    if (appName && *appName) st.appName = appName;
    if (logDir && *logDir)   st.logDir  = logDir;   // default is "." in state

    st.consoleLevel = consoleLevel;
    st.fileLevel    = fileLevel;

    st.startStamp = stampNowYMD_HMS(std::chrono::system_clock::now());

    st.initialized = true;

    std::atexit(flushAtExit);
    std::set_terminate(onTerminate);

    std::signal(SIGINT, onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, onSignal);
#endif

    st.lines.push_back("=== " + st.appName + " session start " + st.startStamp + " ===");
}

void Logger::setPort(int port) {
    LoggerState& st = LS();
    std::lock_guard<std::mutex> lock(st.mu);
    st.port = port;
}

void Logger::log(Level lvl, const char* component, const char* file, int line, const char* func, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(lvl, component, file, line, func, fmt, ap);
    va_end(ap);
}

void Logger::vlog(Level lvl, const char* component, const char* file, int line, const char* func, const char* fmt, va_list ap) {
    LoggerState& st = LS();
    if (!st.initialized) Logger::init("SeedApp");

    char msg[2048];
    msg[0] = '\0';
    if (fmt) {
        std::vsnprintf(msg, sizeof(msg), fmt, ap);
        msg[sizeof(msg) - 1] = '\0';
    }

    // 2026-01-21 09:45:33.173 INFO APP portAllocator.cpp:33 claim - msg
    std::ostringstream oss;
    oss << timestampMs()
        << ' ' << levelName(lvl)
        << ' ' << (component && *component ? component : "APP")
        << ' ' << baseName(file) << ':' << line
        << ' ' << (func ? func : "")
        << " - " << msg;

    const std::string lineStr = oss.str();

    bool toConsole = false;
    bool toFile = false;

    {
        std::lock_guard<std::mutex> lock(st.mu);
        toConsole = (lvl >= st.consoleLevel);
        toFile    = (lvl >= st.fileLevel);

        if (toFile) {
            st.lines.push_back(lineStr);
            if (st.lines.size() > st.maxLines) st.lines.pop_front();
        }
    }

    if (toConsole) {
        std::fprintf(stdout, "%s\n", lineStr.c_str());
        std::fflush(stdout);
    }
}

void Logger::flushToFile() {
    LoggerState& st = LS();

    std::deque<std::string> copy;
    std::string dir, name, startStamp;
    int port = -1;

    {
        std::lock_guard<std::mutex> lock(st.mu);
        if (!st.initialized || st.flushed) return;

        st.flushed = true;

        dir = st.logDir;
        name = st.appName;
        startStamp = st.startStamp;
        port = st.port;

        copy = st.lines;
        copy.push_back("=== " + name + " session end " + stampNowYMD_HMS(std::chrono::system_clock::now()) + " ===");
    }

    // File name: SeedApp_900x_YYYYMMDD_HHMMSS.txt
    std::ostringstream path;
    if (!dir.empty() && dir != ".") path << dir << '/';
    path << name;
    if (port >= 0) path << '_' << port;
    path << '_' << startStamp << ".txt";

    FILE* fp = std::fopen(path.str().c_str(), "wb");
    if (!fp) return;

    for (size_t i = 0; i < copy.size(); ++i) {
        const std::string& s = copy[i];
        std::fwrite(s.c_str(), 1, s.size(), fp);
        std::fwrite("\n", 1, 1, fp);
    }
    std::fclose(fp);
}