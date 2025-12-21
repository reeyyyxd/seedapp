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

    if (prog) {
        prog->active = true;
        prog->success = false;
        prog->failed = false;
        prog->cancel = false;
        prog->doneBytes = 0;
        prog->doneChunks = 0;
    }

    long long size = -1;
    bool metaOk = false;
    for (size_t i = 0; i < seeders.size(); ++i) {
        if (fetchMeta(filename, seeders[i], size)) { metaOk = true; break; }
    }
    if (!metaOk || size < 0) {
        if (prog) { prog->failed = true; prog->active = false; }
        logErr("META failed for %s", filename.c_str());
        return false;
    }

    int totalChunks = (int)((size + (chunkSize_ - 1)) / chunkSize_);
    if (prog) {
        prog->totalBytes = size;
        prog->totalChunks = totalChunks;
    }

    logInfo("Downloading '%s' size=%lld chunks=%d seeders=%d",
            filename.c_str(), size, totalChunks, (int)seeders.size());

    std::string outPath = portDirectory(myPort) + "/" + filename;
    FILE* out = fopen(outPath.c_str(), "wb+");
    if (!out) {
        if (prog) { prog->failed = true; prog->active = false; }
        return false;
    }

    std::vector<char> buf((size_t)chunkSize_);

    for (int chunk = 0; chunk < totalChunks; ++chunk) {
        if (prog && prog->cancel) {
            logWarn("Download cancelled: %s", filename.c_str());
            fclose(out);
            if (prog) { prog->failed = true; prog->active = false; }
            return false;
        }

        bool got = false;
        for (size_t a = 0; a < seeders.size(); ++a) {
            int seederPort = seeders[(chunk + (int)a) % (int)seeders.size()];
            size_t n = 0;

            if (fetchChunk(filename, seederPort, chunk, buf.data(), n)) {
                long long off = (long long)chunk * (long long)chunkSize_;
                if (fseeko(out, (off_t)off, SEEK_SET) != 0) {
                    logErr("fseeko failed at chunk %d (off=%lld)", chunk, off);
                    fclose(out);
                    if (prog) { prog->failed = true; prog->active = false; }
                    return false;
                }

                if (fwrite(buf.data(), 1, n, out) != n) {
                    logErr("fwrite failed at chunk %d", chunk);
                    fclose(out);
                    if (prog) { prog->failed = true; prog->active = false; }
                    return false;
                }

                if (prog) {
                    prog->doneBytes += (long long)n;
                    prog->doneChunks += 1;
                }

                got = true;
                break;
            }
        }

        if (!got) {
            fclose(out);
            if (prog) { prog->failed = true; prog->active = false; }
            return false;
        }
    }

    fclose(out);
    logInfo("Download complete: %s", outPath.c_str());

    if (prog) {
        prog->success = true;
        prog->active = false;
    }
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