#include "../inc/clientsocket.h"
#include "../inc/logger2.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>

clientSocket::clientSocket() : client_fd(-1) {
    client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        logErr("socket() failed: %s", strerror(errno));
        client_fd = -1;
    } else {
        logDbg("socket() ok, fd=%d", client_fd);
    }
}

clientSocket::~clientSocket() {
    closeConn();
}

bool clientSocket::connectServer(const std::string& ip, int port) {
    if (client_fd < 0) {
        logErr("connectServer() called but socket fd is invalid");
        return false;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    int rc = ::inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
    if (rc == 0) {
        logErr("inet_pton(): invalid IPv4 address '%s'", ip.c_str());
        return false;
    }
    if (rc < 0) {
        logErr("inet_pton() failed: %s", strerror(errno));
        return false;
    }

    if (::connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        logErr("connect() to %s:%d failed: %s", ip.c_str(), port, strerror(errno));
        return false;
    }

    logInfo("connected to %s:%d (fd=%d)", ip.c_str(), port, client_fd);
    return true;
}

void clientSocket::closeConn() {
    if (client_fd >= 0) {
        int fd = client_fd;
        ::close(client_fd);
        client_fd = -1;
        logInfo("closed client socket fd=%d", fd);
    }
}

bool clientSocket::sendData(const std::string& data) {
    if (client_fd < 0) {
        logErr("sendData() called but socket fd is invalid");
        return false;
    }
    
    if (!sendAll(data.data(), data.size())) {
        logErr("sendData(): sendAll failed");
        return false;
    }

    logDbg("sent %zu bytes (sendAll)", data.size());
    return true;
}

int clientSocket::receiveData(char* buffer, size_t size) {
    if (client_fd < 0) {
        logErr("receiveData() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || size == 0) {
        logErr("receiveData() invalid buffer/size");
        return -1;
    }

    ssize_t n = ::read(client_fd, buffer, size - 1);
    if (n < 0) {
        logErr("read() failed: %s", strerror(errno));
        return (int)n;
    }

    buffer[n] = '\0';
    logDbg("received %zd bytes", n);
    return (int)n;
}

bool clientSocket::sendAll(const void* data, size_t len) {
    if (client_fd < 0) {
        logErr("sendAll() called but socket fd is invalid");
        return false;
    }
    if (!data && len != 0) {
        logErr("sendAll() invalid data pointer");
        return false;
    }

    const char* p = static_cast<const char*>(data);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::send(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // interrupted, retry
            logErr("send() failed in sendAll(): %s", strerror(errno));
            return false;
        }
        if (n == 0) {
            logErr("send() returned 0 in sendAll() (connection closed?)");
            return false;
        }
        total += static_cast<size_t>(n);
    }

    return true;
}

bool clientSocket::receiveExact(void* buffer, size_t len) {
    if (client_fd < 0) {
        logErr("receiveExact() called but socket fd is invalid");
        return false;
    }
    if (!buffer && len != 0) {
        logErr("receiveExact() invalid buffer pointer");
        return false;
    }

    char* p = static_cast<char*>(buffer);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::recv(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) 
            continue; 

            logErr("recv() failed in receiveExact(): %s", strerror(errno));
            return false;
        }
        if (n == 0) {
            logErr("peer closed connection while receiving exact %zu bytes (got %zu)", len, total);
            return false;
        }
        total += static_cast<size_t>(n);
    }

    return true;
}

int clientSocket::receiveCString(char* buffer, size_t cap) {
    if (client_fd < 0) {
        logErr("receiveCString() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || cap == 0) {
        logErr("receiveCString() invalid buffer/cap");
        return -1;
    }

    size_t i = 0;
    while (i + 1 < cap) { 
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            logErr("recv() failed in receiveCString(): %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            // connection closed
            break;
        }

        buffer[i++] = ch;
        if (ch == '\0') {
            return (int)i;
        }
    }

    buffer[cap - 1] = '\0';
    logWarn("receiveCString(): string exceeded cap=%zu (truncated)", cap);
    return (int)cap;
}

int clientSocket::receiveLine(char* buffer, size_t cap) {
    if (client_fd < 0) {
        logErr("receiveLine() called but socket fd is invalid");
        return 0; 
    }
    if (!buffer || cap == 0) {
        logErr("receiveLine() invalid buffer/cap");
        return 0;
    }

    size_t i = 0;
    bool truncated = false;

    while (true) {
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            logErr("recv() failed in receiveLine(): %s", strerror(errno));
            buffer[i < cap ? i : cap - 1] = '\0';
            return 0;
        }

        if (n == 0) {
            break;
        }

        if (!truncated) {
            if (i + 1 < cap) {
                buffer[i++] = ch;
            } else {
                truncated = true;
            }
        }

        if (ch == '\n') break;
    }

    if (i >= cap) i = cap - 1;
    buffer[i] = '\0';

    if (truncated) {
        logWarn("receiveLine(): line exceeded cap=%zu (drained to newline)", cap);
    }

    return (int)i;
}