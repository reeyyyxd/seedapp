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
    if (seeders.empty()) return false;

    long long size = -1;
    bool metaOk = false;
    for (size_t i = 0; i < seeders.size(); ++i) {
        if (fetchMeta(filename, seeders[i], size)) { metaOk = true; break; }
    }
    if (!metaOk || size < 0) {
        logErr("META failed for %s", filename.c_str());
        return false;
    }

    int totalChunks = (int)((size + (chunkSize_ - 1)) / chunkSize_);
    logInfo("Downloading '%s' size=%lld chunks=%d seeders=%d",
            filename.c_str(), size, totalChunks, (int)seeders.size());

    std::string outPath = portDirectory(myPort) + "/" + filename;
    FILE* out = fopen(outPath.c_str(), "wb+");
    if (!out) return false;

    std::atomic<int> nextChunk(0);
    std::atomic<bool> failed(false);
    std::mutex fileMu;

    int maxThreads = (int)seeders.size();
    if (maxThreads < 1) maxThreads = 1;
    maxThreads = std::min(maxThreads, 4); // cap for now to tune later

    auto worker = [&](int workerId) {
        std::vector<char> localBuf((size_t)chunkSize_);

        while (!failed) {
            int chunk = nextChunk.fetch_add(1);
            if (chunk >= totalChunks) break;

            bool got = false;

            // try seeders 
            for (size_t a = 0; a < seeders.size(); ++a) {
                int seederPort = seeders[(chunk + (int)a) % (int)seeders.size()];

                size_t n = 0;
                if (fetchChunk(filename, seederPort, chunk, localBuf.data(), n)) {
                    long long off = (long long)chunk * (long long)chunkSize_;

                    {
                        std::lock_guard<std::mutex> lock(fileMu);

                        // current position (file-position indicator) of a file stream
                        if (fseeko(out, (off_t)off, SEEK_SET) != 0) {
                            logErr("fseeko failed at chunk %d (off=%lld)", chunk, off);
                            failed = true;
                            break;
                        }

                        if (fwrite(localBuf.data(), 1, n, out) != n) {
                            logErr("fwrite failed at chunk %d", chunk);
                            failed = true;
                            break;
                        }
                    }

                    got = true;
                    break;
                }
            }

            if (!got) {
                logErr("failed to download chunk %d for %s", chunk, filename.c_str());
                failed = true;
                break;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve((size_t)maxThreads);
    for (int i = 0; i < maxThreads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) t.join();

    fclose(out);

    if (failed) return false;

    logInfo("Download complete: %s", outPath.c_str());
    return true;
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