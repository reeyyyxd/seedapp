#ifndef __SEEDSERVER_H__
#define __SEEDSERVER_H__
#include <atomic>
#include <thread>

class SeedServer {
public:
    SeedServer(int chunkSize);
    ~SeedServer();

    bool start(int port, int boundListenFd);
    void stop();

private:
    void serveLoop(int port, int listenFd);
    bool handleMeta(int clientFd, int port, const char* filename);
    bool handleGet(int clientFd, int port, const char* line);

    long long getFileSizeBytes(const char* path);
    bool handleList(int clientFd, int port);

    std::atomic<bool> running_;
    std::thread thread_;
    int chunkSize_;
};
#endif