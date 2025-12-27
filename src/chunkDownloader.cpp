#include "../inc/chunkDownloader.h"
#include "../inc/clientsocket.h"
#include "../inc/logger2.h"
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>


ChunkDownloader::ChunkDownloader(int chunkSize, int startPort, int endPort)
: chunkSize_(chunkSize), startPort_(startPort), endPort_(endPort) {}

std::string ChunkDownloader::portDirectory(int port) const {
    char path[256];
    snprintf(path, sizeof(path), "bin/ports/%d", port);
    return std::string(path);
}

bool ChunkDownloader::fetchMeta(const std::string& filename, int seederPort, long long& outSize) {
    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort)) return false;

    std::string req = "META " + filename + "\n";
    if (!cs.sendData(req)) return false;

    char line[128];
    int n = cs.receiveLine(line, sizeof(line));  
    if (n <= 0) return false;

    if (!strncmp(line, "<FILE_NOT_FOUND>", 15)) return false;

    long long sz = -1;
    if (sscanf(line, "<META> %lld", &sz) == 1 && sz >= 0) {
        outSize = sz;
        return true;
    }
    return false;
}

bool ChunkDownloader::fetchChunk(const std::string& filename, int seederPort, int chunkIndex,
                                 char* outBuf, size_t& outN) {
    outN = 0;

    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort)) return false;

    char req[256];
    snprintf(req, sizeof(req), "GET %s %d\n", filename.c_str(), chunkIndex);
    if (!cs.sendData(std::string(req))) return false;

    char header[128];
    int hn = cs.receiveLine(header, sizeof(header));
    if (hn <= 0) return false;

    if (!strncmp(header, "<FILE_NOT_FOUND>", 15)) return false;
    if (!strncmp(header, "<RANGE_ERROR>", 12)) return false;
    if (!strncmp(header, "<BAD_REQUEST>", 13)) return false;

    int idx = -1, nbytes = 0;
    if (sscanf(header, "<CHUNK> %d %d", &idx, &nbytes) != 2) return false;
    if (idx != chunkIndex || nbytes < 0 || nbytes > chunkSize_) return false;

    if (!cs.receiveExact(outBuf, (size_t)nbytes)) return false;

    outN = (size_t)nbytes;
    return true;
}

bool ChunkDownloader::download(const std::string& filename,
                               const std::vector<int>& seeders,
                               int myPort) {
    return download(filename, seeders, myPort, nullptr);
}

bool ChunkDownloader::download(const std::string& filename,
                               const std::vector<int>& seeders,
                               int myPort,
                               DownloadProgress* prog) {
    if (seeders.empty()) return false;

    // --- INIT PROGRESS ---
    if (prog) {
        prog->reset();
        prog->active.store(true);
    }

    // --- FETCH META ---
    long long size = -1;
    bool metaOk = false;
    for (int p : seeders) {
        if (fetchMeta(filename, p, size)) {
            metaOk = true;
            break;
        }
    }

    if (!metaOk || size < 0) {
        if (prog) {
            prog->failed.store(true);
            prog->active.store(false);
        }
        return false;
    }

    const int totalChunks =
        static_cast<int>((size + chunkSize_ - 1) / chunkSize_);

    if (prog) {
        prog->totalBytes.store(size);
        prog->totalChunks.store(totalChunks);
    }

    // --- OPEN OUTPUT FILE ---
    std::string outPath = portDirectory(myPort) + "/" + filename;
    FILE* out = fopen(outPath.c_str(), "wb+");
    if (!out) {
        if (prog) {
            prog->failed.store(true);
            prog->active.store(false);
        }
        return false;
    }

    std::mutex fileMu;                 // protect fseeko + fwrite
    std::atomic<int> nextChunk{0};     // shared work queue
    std::atomic<bool> anyFailed{false};

    // --- WORKER FUNCTION ---
    auto worker = [&](int seederPort) {
        std::vector<char> buf(chunkSize_);

        while (true) {
            int chunk = nextChunk.fetch_add(1);
            if (chunk >= totalChunks) break;

            size_t n = 0;
            if (!fetchChunk(filename, seederPort, chunk, buf.data(), n)) {
                // Seeder temporarily unavailable → retry later
                prog->pending.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                nextChunk.fetch_sub(1); // put chunk back
                continue;
            }

            long long offset =
                static_cast<long long>(chunk) * chunkSize_;

            {
                std::lock_guard<std::mutex> lock(fileMu);
                fseeko(out, offset, SEEK_SET);
                fwrite(buf.data(), 1, n, out);
            }

            if (prog) {
                prog->pending.store(false);
                prog->doneBytes.fetch_add(n);
                prog->doneChunks.fetch_add(1);
            }
        }
    };

    // --- SPAWN THREADS ---
    std::vector<std::thread> threads;
    for (int p : seeders) {
        threads.emplace_back(worker, p);
    }

    // --- WAIT ---
    for (auto& t : threads) {
        t.join();
    }

    fclose(out);

    // --- FINAL STATE ---
    if (prog) {
        prog->active.store(false);
        if (prog->doneChunks.load() == totalChunks)
            prog->success.store(true);
        else
            prog->failed.store(true);
    }

    return prog && prog->success.load();
}

bool ChunkDownloader::probeSize(const std::string& filename,
                                const std::vector<int>& seeders,
                                long long& outSize) {
    outSize = -1;
    for (int p : seeders) {
        if (fetchMeta(filename, p, outSize)) return true;
    }
    return false;
}