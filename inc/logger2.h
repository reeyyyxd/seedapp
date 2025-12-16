#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>

static inline void log_timestamp(char* out, size_t cap) {
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)

    localtime_s(&tmv, &t);
#else

    localtime_r(&t, &tmv);
#endif

    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
}

#ifdef LOG_TO_FILE
#define log(lvl, fmt, ...) do { \
    FILE* fp = fopen("log.txt", "a"); \
    if (fp) { \
        char ts[20]; \
        log_timestamp(ts, sizeof(ts)); \
        fprintf(fp, "%s %s %s(%d) " fmt "\n", lvl, ts, __func__, __LINE__, ##__VA_ARGS__); \
        fclose(fp); \
    } \
} while(0)
#else
#define log(lvl, fmt, ...) do { \
    char ts[20]; \
    log_timestamp(ts, sizeof(ts)); \
    printf("%s %s %s(%d) " fmt "\n", lvl, ts, __func__, __LINE__, ##__VA_ARGS__); \
} while(0)
#endif

#define logDbg(fmt, ...)  log("[Debug]", fmt, ##__VA_ARGS__)
#define logInfo(fmt, ...) log("[Info ]", fmt, ##__VA_ARGS__)
#define logWarn(fmt, ...) log("[Warn ]", fmt, ##__VA_ARGS__)
#define logErr(fmt, ...)  log("[Error]", fmt, ##__VA_ARGS__)

#endif