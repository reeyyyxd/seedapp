#include "../inc/clientsocket.h"
#include "../inc/logger2.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/time.h>


clientSocket::clientSocket() : client_fd(-1) {
    client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        cErr("socket() failed: %s", strerror(errno));
        client_fd = -1;
    }
    //cDbg("socket() ok, fd=%d", client_fd);
}

clientSocket::~clientSocket() { 
    closeConn();
}


bool clientSocket::connectServer(const std::string& ip, int port) {
    if (client_fd >= 0) {
        ::close(client_fd);
        client_fd = -1;
    }

    client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        cErr("socket() failed: %s", strerror(errno));
        return false;
    }

    // Enable socket options BEFORE connect
    // Disable Nagle's algorithm for better small-packet performance
    int one = 1;
    if (::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        cWarn("setsockopt(TCP_NODELAY) failed: %s", strerror(errno));
    }

    // Set reasonable timeouts to avoid hanging forever
    timeval tv;
    tv.tv_sec = 5;  // 5 second timeout
    tv.tv_usec = 0;
    if (::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        cWarn("setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
    }
    if (::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        cWarn("setsockopt(SO_SNDTIMEO) failed: %s", strerror(errno));
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    int rc = ::inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
    if (rc <= 0) {
        if (rc == 0) {
            cErr("inet_pton(): invalid IPv4 address '%s'", ip.c_str());
        } else {
            cErr("inet_pton() failed: %s", strerror(errno));
        }
        ::close(client_fd);
        client_fd = -1;
        return false;
    }

    if (::connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        cErr("connect() to %s:%d failed: %s", ip.c_str(), port, strerror(errno));
        ::close(client_fd);
        client_fd = -1;
        return false;
    }

    // Only log successful connections (INFO level, not TRACE)
    cInfo("connected to %s:%d", ip.c_str(), port);
    return true;
}


void clientSocket::closeConn() {
    if (client_fd >= 0) {
        ::close(client_fd);
        client_fd = -1;
        //cTrace("closed client socket fd=%d", fd);
    }
}

bool clientSocket::sendData(const std::string& data) {
    if (client_fd < 0) {
        cErr("sendData() called but socket fd is invalid");
        return false;
    }
    
    if (!sendAll(data.data(), data.size())) {
        cErr("sendData(): sendAll failed");
        return false;
    }

    //cTrace("sent %zu bytes (sendAll)", data.size());
    return true;
}

int clientSocket::receiveData(char* buffer, size_t size) {
    if (client_fd < 0) {
        cErr("receiveData() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || size == 0) {
        cErr("receiveData() invalid buffer/size");
        return -1;
    }

    ssize_t n = ::read(client_fd, buffer, size - 1);
    if (n < 0) {
        cErr("read() failed: %s", strerror(errno));
        return (int)n;
    }

    buffer[n] = '\0';
    //cTrace("received %zd bytes", n);
    return (int)n;
}

bool clientSocket::sendAll(const void* data, size_t len) {
    if (client_fd < 0) {
       cErr("sendAll() called but socket fd is invalid");
        return false;
    }
    if (!data && len != 0) {
       cErr("sendAll() invalid data pointer");
        return false;
    }

    const char* p = static_cast<const char*>(data);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::send(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // interrupted, retry
            cErr("send() failed in sendAll(): %s", strerror(errno));
            return false;
        }
        if (n == 0) {
            cErr("send() returned 0 in sendAll() (connection closed?)");
            return false;
        }
        total += static_cast<size_t>(n);
    }

    return true;
}

bool clientSocket::receiveExact(void* buffer, size_t len) {
    if (client_fd < 0) {
        cErr("receiveExact() called but socket fd is invalid");
        return false;
    }
    if (!buffer && len != 0) {
        cErr("receiveExact() invalid buffer pointer");
        return false;
    }

    char* p = static_cast<char*>(buffer);
    size_t total = 0;

    while (total < len) {
        ssize_t n = ::recv(client_fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) 
                continue; 

            cErr("recv() failed in receiveExact(): %s", strerror(errno));
            return false;
        }
        if (n == 0) {
            cErr("peer closed connection while receiving exact %zu bytes (got %zu)", len, total);
            return false;
        }
        total += static_cast<size_t>(n);
    }

    return true;
}

int clientSocket::receiveCString(char* buffer, size_t cap) {
    if (client_fd < 0) {
        cErr("receiveCString() called but socket fd is invalid");
        return -1;
    }
    if (!buffer || cap == 0) {
        cErr("receiveCString() invalid buffer/cap");
        return -1;
    }

    size_t i = 0;
    while (i + 1 < cap) { 
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            cErr("recv() failed in receiveCString(): %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            break;
        }

        buffer[i++] = ch;
        if (ch == '\0') {
            return (int)i;
        }
    }

    buffer[cap - 1] = '\0';
    cWarn("receiveCString(): string exceeded cap=%zu (truncated)", cap);
    return (int)cap;
}

int clientSocket::receiveLine(char* buffer, size_t cap) {
    if (client_fd < 0) {
        cErr("receiveLine() called but socket fd is invalid");
        return 0; 
    }
    if (!buffer || cap == 0) {
        cErr("receiveLine() invalid buffer/cap");
        return 0;
    }

    size_t i = 0;
    bool truncated = false;

    while (true) {
        char ch;
        ssize_t n = ::recv(client_fd, &ch, 1, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            cErr("recv() failed in receiveLine(): %s", strerror(errno));
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
        cWarn("receiveLine(): line exceeded cap=%zu (drained to newline)", cap);
    }

    return (int)i;
}
