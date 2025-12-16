#include "../inc/netIO.h"
#include <sys/socket.h>
#include <errno.h>

namespace NetIo {

int recvLine(int fd, char* out, size_t cap) {
    if (!out || cap == 0) return -1;
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        out[i++] = c;
        if (c == '\n') break;
    }
    out[i] = '\0';
    return (int)i;
}

bool recvAll(int fd, void* buf, size_t len) {
    char* p = (char*)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        got += (size_t)n;
    }
    return true;
}

bool sendAll(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

}