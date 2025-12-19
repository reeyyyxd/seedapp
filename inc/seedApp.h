#ifndef SEED_APP_H
#define SEED_APP_H

#include "portAllocator.h"
#include "seedServer.h"
#include "fileScanner.h"
#include "chunkDownloader.h"

class SeedApp {
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