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
#include <chrono>   // <-- needed for sleep_for

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

bool ChunkDownloader::fetchChunk(
    const std::string& filename,
    int seederPort,
    int chunkIndex,
    char* outBuf,
    size_t& outN,
    int* outCode
) {
    outN = 0;
    if (outCode) *outCode = 1;

    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort)) return false;

    // IMPORTANT: filename may contain spaces; server-side parser must support it (yours does via last-space parse)
    char req[256];
    snprintf(req, sizeof(req), "GET %s %d\n", filename.c_str(), chunkIndex);
    if (!cs.sendData(std::string(req))) return false;

    char header[128];
    int hn = cs.receiveLine(header, sizeof(header));
    if (hn <= 0) return false;

    if (!strncmp(header, "<FILE_NOT_FOUND>", 15)) {
        if (outCode) *outCode = 2;
        return false;
    }
    if (!strncmp(header, "<RANGE_ERROR>", 12) || !strncmp(header, "<BAD_REQUEST>", 13)) {
        if (outCode) *outCode = 3;
        return false;
    }

    int idx = -1, nbytes = 0;
    if (sscanf(header, "<CHUNK> %d %d", &idx, &nbytes) != 2) return false;
    if (idx != chunkIndex || nbytes < 0 || nbytes > chunkSize_) return false;

    if (!cs.receiveExact(outBuf, (size_t)nbytes)) return false;

    outN = (size_t)nbytes;
    if (outCode) *outCode = 0;
    return true;
}

bool ChunkDownloader::download(
    const std::string& filename,
    const std::vector<int>& seeders,
    int myPort
) {
    return download(filename, seeders, myPort, nullptr);
}

bool ChunkDownloader::download(
    const std::string& filename,
    const std::vector<int>& seeders,
    int myPort,
    DownloadProgress* prog
) {
    if (seeders.empty()) return false;

    if (prog) {
        prog->reset();
        prog->active.store(true);
    }

    // --- META ---
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
        logErr("DL meta failed file='%s' seeders=%zu", filename.c_str(), seeders.size());
        return false;
    }

    const int totalChunks = static_cast<int>((size + chunkSize_ - 1) / chunkSize_);

    if (prog) {
        prog->totalBytes.store(size);
        prog->totalChunks.store(totalChunks);
    }

    logInfo("DL start file='%s' size=%lld chunks=%d seeders=%zu myPort=%d",
            filename.c_str(), size, totalChunks, seeders.size(), myPort);

    std::string outPath = portDirectory(myPort) + "/" + filename;
    std::string tmpPath = outPath + ".part";
    FILE* out = fopen(tmpPath.c_str(), "wb+");
    if (!out) {
        if (prog) {
            prog->failed.store(true);
            prog->active.store(false);
        }
        logErr("DL open failed tmp='%s'", tmpPath.c_str());
        return false;
    }

    std::mutex fileMu;
    std::atomic<int> nextChunk{0};
    std::atomic<bool> anyFailed{false};

    // Log "pending/resumed" only when state changes (no spam)
    std::atomic<bool> pendingLogged{false};
    std::atomic<bool> resumedLogged{false};

    auto worker = [&](int workerId) {
        std::vector<char> buf(chunkSize_);
        int backoffMs = 200;

        while (true) {
            int chunk = nextChunk.fetch_add(1);
            if (chunk >= totalChunks) break;

            size_t n = 0;
            bool ok = false;

            while (!ok) {
                bool sawTempFail = false;
                bool sawPermanentFail = false;

                for (size_t k = 0; k < seeders.size(); ++k) {
                    int p = seeders[(workerId + (int)k) % (int)seeders.size()];

                    int code = 1;
                    if (fetchChunk(filename, p, chunk, buf.data(), n, &code)) {
                        ok = true;
                        break;
                    }

                    if (code == 1) {
                        sawTempFail = true;     // temp: connect/recv/etc
                    } else {
                        sawPermanentFail = true; // file not found / range / bad request
                    }
                }

                if (!ok) {
                    if (sawTempFail) {
                        if (prog) prog->pending.store(true);

                        // Log once when entering "Looking for seeders"
                        if (!pendingLogged.exchange(true)) {
                            logWarn("DL pending (looking for seeders) file='%s' chunk=%d backoffMs=%d",
                                    filename.c_str(), chunk, backoffMs);
                            resumedLogged.store(false);
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
                        backoffMs = std::min(backoffMs * 2, 2000);
                        continue;
                    }

                    // Permanent failure
                    anyFailed.store(true);
                    if (prog) {
                        prog->pending.store(false);
                        prog->failed.store(true);
                        prog->active.store(false);
                    }

                    logErr("DL failed permanent file='%s' chunk=%d (no usable seeder response)",
                           filename.c_str(), chunk);
                    return;
                }

                // Success after pending -> log one "resumed"
                backoffMs = 200;
                if (prog) prog->pending.store(false);

                if (pendingLogged.load() && !resumedLogged.exchange(true)) {
                    logInfo("DL resumed file='%s'", filename.c_str());
                    pendingLogged.store(false);
                }
            }

            long long offset = (long long)chunk * (long long)chunkSize_;
            {
                std::lock_guard<std::mutex> lock(fileMu);
                fseek(out, offset, SEEK_SET);
                fwrite(buf.data(), 1, n, out);
            }

            if (prog) {
                prog->doneBytes.fetch_add(n);
                prog->doneChunks.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    int numThreads = (int)seeders.size();
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    fclose(out);

    bool completed = (prog ? prog->doneChunks.load() == totalChunks : (!anyFailed.load()));
    if (completed) {
        std::remove(outPath.c_str());
        if (std::rename(tmpPath.c_str(), outPath.c_str()) != 0) {
            if (prog) {
                prog->failed.store(true);
                prog->success.store(false);
                prog->active.store(false);
            }
            logErr("DL rename failed tmp='%s' -> out='%s'", tmpPath.c_str(), outPath.c_str());
            return false;
        }
    } else {
        // keep .part for resume/debug
        // std::remove(tmpPath.c_str());
    }

    if (prog) {
        prog->active.store(false);
        if (prog->doneChunks.load() == totalChunks) {
            prog->success.store(true);
        } else {
            prog->failed.store(true);
        }
    }

    if (completed) {
        logInfo("DL complete file='%s' bytes=%lld chunks=%d", filename.c_str(), size, totalChunks);
    } else {
        int done = prog ? prog->doneChunks.load() : -1;
        logErr("DL incomplete file='%s' doneChunks=%d/%d", filename.c_str(), done, totalChunks);
    }

    bool ok = (!anyFailed.load());
    if (prog) ok = prog->success.load();
    return ok;
}

bool ChunkDownloader::probeSize(
    const std::string& filename,
    const std::vector<int>& seeders,
    long long& outSize
) {
    outSize = -1;
    for (int p : seeders) {
        if (fetchMeta(filename, p, outSize)) return true;
    }
    return false;
}