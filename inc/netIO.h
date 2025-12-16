#ifndef __NETIO_H__
#define __NETIO_H__
#include <cstddef>

namespace NetIo {
    int  recvLine(int fd, char* out, size_t cap);          
    bool recvAll(int fd, void* buf, size_t len);           
    bool sendAll(int fd, const void* buf, size_t len);    
}
#endif