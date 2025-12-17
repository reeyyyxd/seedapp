#ifndef __CLIENTSOCKET_H__
#define __CLIENTSOCKET_H__

#include <string>
#include <arpa/inet.h>
#include <stddef.h> 

class clientSocket {
public:
    clientSocket();
    ~clientSocket();

    bool connectServer(const std::string& ip, int port);

   
    bool sendData(const std::string& data);
    bool sendAll(const void* data, size_t len);

    int receiveData(char* buffer, size_t size);

    int  receiveCString(char* buffer, size_t cap);

    bool receiveExact(void* buffer, size_t len);

    int receiveLine(char* buffer, size_t cap);
    

    void closeConn();

private:
    int client_fd;
    struct sockaddr_in serv_addr;
};

#endif