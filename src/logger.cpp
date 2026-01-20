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
#include <thread>

#if defined(_WIN32)
  #include <direct.h>
#else
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

namespace {

struct State {
    bool initialized = false;
    bool flushed = false;

    std::string appName = "SeedApp";
    std::string logDir = "logs";
    int port = -1;

    Logger::Level consoleLevel = Logger::INFO; // keep console clean
    Logger::Level fileLevel = Logger::TRACE;   // capture everything in file

    std::chrono::system_clock::time_point start;
    std::string stamp; // YYYYMMDD_HHMMSS

    std::mutex mu;
    std::deque<std::string> lines;
    size_t maxLines = 50000; // safety cap (prevents RAM blowup)
};

State& S() { static State s; return s; }

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

static std::string stampNow(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y%m%d_%H%M%S");
    return oss.str();
}

static std::string timestampMs() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    std::tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

static std::string threadIdShort() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    std::string s = oss.str();
    if (s.size() > 8) s = s.substr(s.size() - 8);
    return s;
}

static void ensureDir(const std::string& dir) {
#if defined(_WIN32)
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
}

static void flushAtExit() {
    Logger::flushToFile();
}

} // namespace

Logger::Logger() {}

void Logger::init(const char* appName, Level consoleLevel, Level fileLevel, const char* logDir) {
    State& st = S();
    std::lock_guard<std::mutex> lock(st.mu);

    if (st.initialized) return;

    if (appName && *appName) st.appName = appName;
    if (logDir && *logDir) st.logDir = logDir;

    st.consoleLevel = consoleLevel;
    st.fileLevel = fileLevel;

    st.start = std::chrono::system_clock::now();
    st.stamp = stampNow(st.start);

    st.initialized = true;

    std::atexit(flushAtExit);

    st.lines.push_back("=== " + st.appName + " session start " + st.stamp + " ===");
}

void Logger::setPort(int port) {
    State& st = S();
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
    State& st = S();
    if (!st.initialized) Logger::init("SeedApp");

    char msg[2048];
    msg[0] = '\0';
    if (fmt) {
        vsnprintf(msg, sizeof(msg), fmt, ap);
        msg[sizeof(msg) - 1] = '\0';
    }

    std::ostringstream oss;
    oss << timestampMs()
        << ' ' << levelName(lvl)
        << ' ' << (component && *component ? component : "APP")
        << " T#" << threadIdShort()
        << ' ' << baseName(file) << ':' << line
        << ' ' << (func ? func : "")
        << " - " << msg;

    const std::string lineStr = oss.str();

    bool toConsole = false;
    bool toFile = false;

    {
        std::lock_guard<std::mutex> lock(st.mu);
        toConsole = (lvl >= st.consoleLevel);
        toFile = (lvl >= st.fileLevel);

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
    State& st = S();

    std::deque<std::string> copy;
    std::string dir, name, stamp;
    int port = -1;

    {
        std::lock_guard<std::mutex> lock(st.mu);
        if (!st.initialized || st.flushed) return;

        st.flushed = true;
        dir = st.logDir;
        name = st.appName;
        stamp = st.stamp;
        port = st.port;
        copy = st.lines;

        copy.push_back("=== " + name + " session end " + stampNow(std::chrono::system_clock::now()) + " ===");
    }

    ensureDir(dir);

    std::ostringstream path;
    path << dir << '/' << name;
    if (port >= 0) path << "_port" << port;
    path << '_' << stamp << ".txt";

    FILE* fp = std::fopen(path.str().c_str(), "wb");
    if (!fp) return;

    for (size_t i = 0; i < copy.size(); ++i) {
        std::fwrite(copy[i].c_str(), 1, copy[i].size(), fp);
        std::fwrite("\n", 1, 1, fp);
    }
    std::fclose(fp);
}