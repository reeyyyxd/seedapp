#ifndef SERVER_SOCKET_H
#define SERVER_SOCKET_H

#include <netinet/in.h>
#include <sys/types.h>

class serversocket {
public:
    explicit serversocket(int port);

    int  create();
    int  bind_listen();
    int  accept(struct sockaddr_in& addr, socklen_t& addrlen);
    int  fd() const { return server_fd_; }
    void close_fd(int fd);
    ssize_t read(int sock, void* buf, size_t len);
    ssize_t send(int sock, const void* buf, size_t len);

    void setSocket(int fd);
    int  listen_only();

private:
    int port_;
    int server_fd_;
};

#endif
