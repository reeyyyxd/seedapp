#ifndef SEED_APP_H
#define SEED_APP_H

#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <memory>
#include <string>

#include "portAllocator.h"
#include "seedServer.h"
#include "fileScanner.h"
#include "chunkDownloader.h"

class SeedApp {

private:
    struct DownloadJob {
        std::string filename;
        std::vector<int> seeders;
        std::chrono::steady_clock::time_point start;
        DownloadProgress progress;
        std::thread worker;
        long long lastDoneBytes = 0;
        std::chrono::steady_clock::time_point lastTick;
    };

public:
    SeedApp(int startPort, int endPort, int chunkSize);
    int run();

private:
    bool boot();
    void shutdown();

    void showMenu() const;
    int readInt(bool* eof) const;

    void downloadFlow();
    void statusFlow();
    void handleClient(int clientFd, int port);

private:
    std::mutex jobsMu_;
    std::vector<std::unique_ptr<DownloadJob>> jobs_;

private:
    int startPort_;
    int endPort_;
    int chunkSize_;

    int myPort_;
    int listenFd_ = -1;

    PortAllocator allocator_;
    SeedServer server_;
    FileScanner scanner_;
    ChunkDownloader downloader_;
};

#endif