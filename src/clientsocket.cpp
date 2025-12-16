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

    ssize_t sent = ::send(client_fd, data.c_str(), data.size(), 0);
    if (sent < 0) {
        logErr("send() failed: %s", strerror(errno));
        return false;
    }

    if ((size_t)sent != data.size()) {
        logWarn("partial send: sent=%zd expected=%zu", sent, data.size());
    } else {
        logDbg("sent %zd bytes", sent);
    }

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