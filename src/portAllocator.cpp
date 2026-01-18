#include "../inc/portAllocator.h"
#include "../inc/logger2.h"
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

PortAllocator::PortAllocator() : port_(-1), listenFd_(-1) {}
PortAllocator::~PortAllocator() { release(); }

int PortAllocator::takeFd() {
    int fd = listenFd_;
    listenFd_ = -1;    
    return fd;
}

bool PortAllocator::claim(int startPort, int endPort) {
    for (int p = startPort; p <= endPort; ++p) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;

        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(p);

        if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            port_ = p;
            listenFd_ = fd;
            LWLOG_INFO("ALLOC", "claimed port=%d fd=%d", port_, listenFd_);
            return true;
        }
        ::close(fd);
    }

    LWLOG_WARN("ALLOC", "no available port in range %d-%d", startPort, endPort);
    return false;
}

void PortAllocator::release() {
    if (listenFd_ >= 0) {
    int fd = listenFd_;
    int p  = port_;
    ::close(listenFd_);
    listenFd_ = -1;
    port_ = -1;
    LWLOG_INFO("ALLOC", "released port=%d fd=%d", p, fd);
}

    
}