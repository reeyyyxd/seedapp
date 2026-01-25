#include "../inc/serversocket.h"
#include "../inc/logger2.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

serversocket::serversocket(int port) : port_(port), server_fd_(-1) {}

int serversocket::create() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        logErr("socket() failed: %s", strerror(errno));
        return -1;
    }

    sDbg("socket() ok, fd=%d", server_fd_);
    return 0;
}

int serversocket::bind_listen() {
    if (server_fd_ < 0) {
        sErr("bind_listen() called but server fd is invalid");
        return -1;
    }

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(port_);

    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        sWarn("setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
    }

    sInfo("Listening on port %d...", port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        logErr("bind() failed: %s", strerror(errno));
        return -1;
    }

    if (::listen(server_fd_,50) < 0) {
        sErr("listen() failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int serversocket::accept(struct sockaddr_in& addr, socklen_t& addrlen) {
    int s = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (s < 0) {
        sErr("accept() failed: %s", strerror(errno));
        return -1;
    }

    //super
    //sTrace("accepted client fd=%d", s);
    return s;
}

ssize_t serversocket::read(int sock, void* buf, size_t len) {
    ssize_t n = ::read(sock, buf, len);
    if (n < 0) {
        sErr("read() failed (fd=%d): %s", sock, strerror(errno));
    }
    return n;
}

ssize_t serversocket::send(int sock, const void* buf, size_t len) {
    ssize_t n = ::send(sock, buf, len, 0);
    if (n < 0) {
        sErr("send() failed (fd=%d): %s", sock, strerror(errno));
    }
    return n;
}

void serversocket::setSocket(int fd) {
    server_fd_ = fd;
}

int serversocket::listen_only() {
    if (server_fd_ < 0) {
        sErr("listen_only() called but server fd is invalid");
        return -1;
    }

    if (::listen(server_fd_, 50) < 0) {
        sErr("listen() failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

void serversocket::close_fd(int fd) {
    if (fd >= 0) {
        ::close(fd);
        sDbg("Closed fd=%d", fd);
    }
}