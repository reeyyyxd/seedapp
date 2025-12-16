#ifndef __PORTALLOCATOR_H__
#define __PORTALLOCATOR_H__
#include <netinet/in.h>

class PortAllocator {
public:
    PortAllocator();
    ~PortAllocator();

    bool claim(int startPort, int endPort);
    int  port() const { return port_; }
    int  fd()   const { return listenFd_; }
    void release();

private:
    int port_;
    int listenFd_;
};
#endif