#include "../inc/clientsocket.h"
#include "../inc/logger2.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>

clientSocket::clientSocket() : client_fd(-1) {
    client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "socket() failed: %s", strerror(errno));
        client_fd = -1;
    } else {
        LWLOG_TRACE("CLIENT", "socket() ok fd=%d", client_fd);
    }
}

clientSocket::~clientSocket() {
    closeConn();
}

bool clientSocket::connectServer(const std::string& ip, int port) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "connectServer() called but socket fd is invalid");
        return false;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    int rc = ::inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
    if (rc == 0) {
        LWLOG_ERROR("CLIENT", "inet_pton(): invalid IPv4 address '%s'", ip.c_str());
        return false;
    }
    if (rc < 0) {
        LWLOG_ERROR("CLIENT", "inet_pton() failed: %s", strerror(errno));
        return false;
    }

    if (::connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        LWLOG_WARN("CLIENT", "connect() failed ip=%s port=%d err=%s", ip.c_str(), port, strerror(errno));
        return false;
    }

    LWLOG_DEBUG("CLIENT", "connected ip=%s port=%d fd=%d", ip.c_str(), port, client_fd);
    return true;
}

void clientSocket::closeConn() {
    if (client_fd >= 0) {
        int fd = client_fd;
        ::close(client_fd);
        client_fd = -1;
        LWLOG_DEBUG("CLIENT", "closed client socket fd=%d", fd);
    }
}

bool clientSocket::sendData(const std::string& data) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "sendData() called but socket fd is invalid");
        return false;
    }

    if (!sendAll(data.data(), data.size())) {
        LWLOG_ERROR("NET", "sendData(): sendAll failed");
        return false;
    }

    LWLOG_TRACE("NET", "sendData ok bytes=%zu", data.size());
    return true;
}

int clientSocket::receiveData(char* buffer, size_t size) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "receiveData() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || size == 0) {
        LWLOG_ERROR("CLIENT", "receiveData() invalid buffer/size");
        return -1;
    }

    ssize_t n = ::read(client_fd, buffer, size - 1);
    if (n < 0) {
        LWLOG_WARN("NET", "read() failed fd=%d: %s", client_fd, strerror(errno));
        return (int)n;
    }

    buffer[n] = '\0';
    LWLOG_TRACE("NET", "read ok fd=%d bytes=%zd", client_fd, n);
    return (int)n;
}

bool clientSocket::sendAll(const void* data, size_t len) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "sendAll() called but socket fd is invalid");
        return false;
    }
    if (!data && len != 0) {
        LWLOG_ERROR("CLIENT", "sendAll() invalid data pointer");
        return false;
    }

    const char* p = static_cast<const char*>(data);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::send(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            LWLOG_WARN("NET", "send() failed fd=%d: %s", client_fd, strerror(errno));
            return false;
        }
        if (n == 0) {
            LWLOG_WARN("NET", "send() returned 0 fd=%d (connection closed?)", client_fd);
            return false;
        }
        total += static_cast<size_t>(n);
    }

    LWLOG_TRACE("NET", "sendAll ok fd=%d bytes=%zu", client_fd, len);
    return true;
}

bool clientSocket::receiveExact(void* buffer, size_t len) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "receiveExact() called but socket fd is invalid");
        return false;
    }
    if (!buffer && len != 0) {
        LWLOG_ERROR("CLIENT", "receiveExact() invalid buffer pointer");
        return false;
    }

    char* p = static_cast<char*>(buffer);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::recv(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            LWLOG_WARN("NET", "recv() failed fd=%d: %s", client_fd, strerror(errno));
            return false;
        }
        if (n == 0) {
            LWLOG_WARN("NET", "peer closed while receiveExact fd=%d need=%zu got=%zu", client_fd, len, total);
            return false;
        }
        total += static_cast<size_t>(n);
    }

    LWLOG_TRACE("NET", "receiveExact ok fd=%d bytes=%zu", client_fd, len);
    return true;
}

int clientSocket::receiveCString(char* buffer, size_t cap) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "receiveCString() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || cap == 0) {
        LWLOG_ERROR("CLIENT", "receiveCString() invalid buffer/cap");
        return -1;
    }

    size_t i = 0;
    while (i + 1 < cap) {
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            LWLOG_WARN("NET", "recv() failed in receiveCString fd=%d: %s", client_fd, strerror(errno));
            return -1;
        }
        if (n == 0) {
            break;
        }

        buffer[i++] = ch;
        if (ch == '\0') {
            LWLOG_TRACE("NET", "receiveCString ok fd=%d bytes=%zu", client_fd, i);
            return (int)i;
        }
    }

    buffer[cap - 1] = '\0';
    LWLOG_WARN("NET", "receiveCString(): exceeded cap=%zu (truncated) fd=%d", cap, client_fd);
    return (int)cap;
}

int clientSocket::receiveLine(char* buffer, size_t cap) {
    if (client_fd < 0) {
        LWLOG_ERROR("CLIENT", "receiveLine() called but socket fd is invalid");
        return 0;
    }
    if (!buffer || cap == 0) {
        LWLOG_ERROR("CLIENT", "receiveLine() invalid buffer/cap");
        return 0;
    }

    size_t i = 0;
    bool truncated = false;

    while (true) {
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            LWLOG_WARN("NET", "recv() failed in receiveLine fd=%d: %s", client_fd, strerror(errno));
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
        LWLOG_WARN("NET", "receiveLine(): exceeded cap=%zu (drained to newline) fd=%d", cap, client_fd);
    } else {
        LWLOG_TRACE("NET", "receiveLine ok fd=%d bytes=%zu", client_fd, i);
    }

    return (int)i;
}