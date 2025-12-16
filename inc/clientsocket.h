#ifndef __CLIENTSOCKET_H__
#define __CLIENTSOCKET_H__

#include <string>
#include <arpa/inet.h>

class clientSocket {

public:
    clientSocket();
    ~clientSocket();

    bool connectServer(const std::string& ip, int port);
    bool sendData(const std::string& data);
    int receiveData(char* buffer, size_t size);
    void closeConn();
    
private:
    int client_fd;
    struct sockaddr_in serv_addr;

};

#endif
