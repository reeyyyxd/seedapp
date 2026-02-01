#include "../inc/chunkDownloader.h"
#include "../inc/clientsocket.h"
#include "../inc/logger2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

static inline void stripCRLF(char* s) {
    if (!s) return;
    size_t L = std::strlen(s);
    while (L > 0 && (s[L - 1] == '\n' || s[L - 1] == '\r')) {
        s[L - 1] = '\0';
        --L;
    }
}

static inline bool startsWith(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    const size_t n = std::strlen(prefix);
    return std::strncmp(s, prefix, n) == 0;
}

ChunkDownloader::ChunkDownloader(int chunkSize, int startPort, int endPort)
    : chunkSize_(chunkSize), startPort_(startPort), endPort_(endPort) {}

std::string ChunkDownloader::portDirectory(int port) const {
    char path[256];
    std::snprintf(path, sizeof(path), "bin/ports/%d", port);
    return std::string(path);
}

bool ChunkDownloader::fetchMeta(const std::string& filename, int seederPort, long long& outSize) {
    outSize = -1;

    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort))
        return false;

    const std::string req = "META " + filename + "\n";
    if (!cs.sendData(req))
        return false;

    char line[256];
    const int n = cs.receiveLine(line, sizeof(line));
    if (n <= 0)
        return false;

    stripCRLF(line);

    if (std::strcmp(line, "<FILE_NOT_FOUND>") == 0) return false;
    if (std::strcmp(line, "<BAD_REQUEST>") == 0)     return false;

    if (!startsWith(line, "<META>"))
        return false;

    long long sz = -1;
    if (std::sscanf(line, "<META> %lld", &sz) == 1 && sz >= 0) {
        outSize = sz;
        return true;
    }

    return false;
}

bool ChunkDownloader::fetchChunk(const std::string& filename,
                                clientSocket& cs,
                                int chunkIndex,
                                char* outBuf,
                                size_t& outN,
                                int* outCode)
{
    outN = 0;
    if (outCode) *outCode = 1;

    char req[512];
    std::snprintf(req, sizeof(req), "GET %s %d\n", filename.c_str(), chunkIndex);
    if (!cs.sendData(std::string(req)))
        return false;

    char header[256];
    const int hn = cs.receiveLine(header, sizeof(header));
    if (hn <= 0) {
        if (outCode) *outCode = 1;
        return false;
    }

    stripCRLF(header);

    if (std::strcmp(header, "<FILE_NOT_FOUND>") == 0) {
        if (outCode) *outCode = 2;
        return false;
    }
    if (std::strcmp(header, "<RANGE_ERROR>") == 0 || std::strcmp(header, "<BAD_REQUEST>") == 0) {
        if (outCode) *outCode = 3;
        return false;
    }

    int idx = -1;
    int nbytes = -1;
    if (std::sscanf(header, "<CHUNK> %d %d", &idx, &nbytes) != 2) {
        if (outCode) *outCode = 1;
        return false;
    }
    if (idx != chunkIndex || nbytes < 0 || nbytes > chunkSize_) {
        if (outCode) *outCode = 3;
        return false;
    }

    if (nbytes == 0) {
        outN = 0;
        if (outCode) *outCode = 0;
        return true;
    }

    if (!cs.receiveExact(outBuf, (size_t)nbytes)) {
        if (outCode) *outCode = 1;
        return false;
    }

    outN = (size_t)nbytes;
    if (outCode) *outCode = 0;
    return true;
}

static bool mergeParts(const std::string& outPath, const std::vector<std::string>& partPaths) {
    FILE* out = std::fopen(outPath.c_str(), "wb");
    if (!out) {
        logErr("mergeParts: cannot open output '%s'", outPath.c_str());
        return false;
    }

    std::vector<char> buf(64 * 1024);

    for (size_t i = 0; i < partPaths.size(); ++i) {
        FILE* in = std::fopen(partPaths[i].c_str(), "rb");
        if (!in) {
            logErr("mergeParts: cannot open part '%s'", partPaths[i].c_str());
            std::fclose(out);
            return false;
        }

        while (true) {
            size_t rd = std::fread(buf.data(), 1, buf.size(), in);
            if (rd > 0) {
                size_t written = std::fwrite(buf.data(), 1, rd, out);
                if (written != rd) {
                    logErr("mergeParts: write failed for '%s'", outPath.c_str());
                    std::fclose(in);
                    std::fclose(out);
                    return false;
                }
            }
            if (rd < buf.size()) break;
        }

        std::fclose(in);
    }

    std::fclose(out);
    logInfo("mergeParts: merged %zu parts into '%s'", partPaths.size(), outPath.c_str());
    return true;
}

bool ChunkDownloader::download(const std::string& filename,
                              const std::vector<int>& seeders,
                              int myPort)
{
    return download(filename, seeders, myPort, nullptr);
}

bool ChunkDownloader::download(const std::string& filename,
                              const std::vector<int>& seeders,
                              int myPort,
                              DownloadProgress* prog)
{
    if (seeders.empty()) {
        logErr("DL: no seeders provided for '%s'", filename.c_str());
        return false;
    }

    if (prog) {
        prog->reset();
        prog->active.store(true);
        prog->pending.store(false);
        prog->failed.store(false);
        prog->success.store(false);
    }

    //meta
    long long fileSize = -1;
    bool metaOk = false;
    for (size_t i = 0; i < seeders.size(); ++i) {
        if (fetchMeta(filename, seeders[i], fileSize)) {
            metaOk = true;
            break;
        }
    }
    if (!metaOk || fileSize < 0) {
        if (prog) {
            prog->failed.store(true);
            prog->active.store(false);
        }
        logErr("DL meta failed file='%s' seeders=%zu", filename.c_str(), seeders.size());
        return false;
    }

    const int totalChunks = (int)((fileSize + chunkSize_ - 1) / chunkSize_);
    if (prog) {
        prog->totalBytes.store(fileSize);
        prog->totalChunks.store(totalChunks);
    }

    logInfo("DL start file='%s' size=%lld chunks=%d seeders=%zu", 
            filename.c_str(), fileSize, totalChunks, seeders.size());

    // ALWAYS create parts equal to number of seeders (even if 1)
    const int parts = (int)seeders.size();

    const std::string baseDir = portDirectory(myPort);
    const std::string outPath = baseDir + "/" + filename;

    // creating part paths
    std::vector<std::string> partPaths((size_t)parts);
    for (int i = 0; i < parts; ++i) {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), ".part%d", i);
        partPaths[(size_t)i] = outPath + suffix;
        std::remove(partPaths[(size_t)i].c_str());
    }

    // Split chunk ranges among parts: [start, end)
    struct Range { int start; int end; };
    std::vector<Range> ranges((size_t)parts);
    for (int i = 0; i < parts; ++i) {
        const int start = (totalChunks * i) / parts;
        const int end   = (totalChunks * (i + 1)) / parts;
        ranges[(size_t)i] = Range{ start, end };
        logInfo("Part %d: chunks %d-%d (seeder port %d)", 
                i, start, end - 1, seeders[i]);
    }

    std::atomic<bool> anyFailed(false);
    std::vector<std::thread> threads;
    threads.reserve(parts);

    // Create worker threads
    for (int i = 0; i < parts; ++i) {
    const Range r = ranges[(size_t)i];
    const std::string partPath = partPaths[(size_t)i];
    const std::string fnCopy = filename;

    // Capture the whole seeders list (by value) so the thread can failover
    threads.push_back(std::thread([this, seeders, i, r, partPath, fnCopy, prog, &anyFailed]() {
        if (r.start >= r.end) {
            logWarn("Empty segment for worker %d", i);
            return;
        }

        FILE* out = std::fopen(partPath.c_str(), "wb");
        if (!out) {
            anyFailed.store(true);
            if (prog) {
                prog->failed.store(true);
                prog->active.store(false);
            }
            logErr("DL: cannot create part '%s'", partPath.c_str());
            return;
        }

        std::vector<char> buf((size_t)chunkSize_);
        clientSocket cs;

        // ---- FAILOVER STATE ----
        size_t curIdx = (size_t)i % seeders.size();          // start with "assigned" seeder
        int seederPort = seeders[curIdx];
        std::vector<bool> dead(seeders.size(), false);       // local dead list for this worker

        auto pickNextSeeder = [&]() -> bool {
            // mark current as dead
            dead[curIdx] = true;

            // find next alive seeder
            for (size_t k = 0; k < seeders.size(); ++k) {
                size_t cand = (curIdx + 1 + k) % seeders.size();
                if (!dead[cand]) {
                    curIdx = cand;
                    seederPort = seeders[curIdx];
                    return true;
                }
            }
            return false; // no seeders left
        };

        bool connected = false;
        int consecutiveFailures = 0;

        // You can tune these:
        const int MAX_RETRIES_PER_SEEDER = 3;   // how many temp failures before switching seeders
        const int MAX_SEEDER_SWITCHES = (int)seeders.size(); // full cycle

        int seederSwitchCount = 0;

        int chunk = r.start;
        while (chunk < r.end && !anyFailed.load()) {

            // Establish connection
            if (!connected) {
                if (prog) prog->pending.store(true);

                if (!cs.connectServer("127.0.0.1", seederPort)) {
                    consecutiveFailures++;

                    if (consecutiveFailures >= MAX_RETRIES_PER_SEEDER) {
                        logWarn("DL: seeder %d seems down, switching (worker %d)", seederPort, i);
                        cs.closeConn();
                        connected = false;
                        consecutiveFailures = 0;

                        if (!pickNextSeeder()) {
                            logErr("DL: no seeders left for worker %d", i);
                            anyFailed.store(true);
                            if (prog) {
                                prog->failed.store(true);
                                prog->active.store(false);
                                prog->pending.store(false);
                            }
                            break;
                        }

                        seederSwitchCount++;
                        if (seederSwitchCount > MAX_SEEDER_SWITCHES) {
                            logErr("DL: too many seeder switches (worker %d)", i);
                            anyFailed.store(true);
                            if (prog) {
                                prog->failed.store(true);
                                prog->active.store(false);
                                prog->pending.store(false);
                            }
                            break;
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }

                connected = true;
                consecutiveFailures = 0;
                if (prog) prog->pending.store(false);

                logInfo("DL: worker %d connected to seeder %d for chunks %d-%d",
                        i, seederPort, r.start, r.end - 1);
            }

            // Fetch chunk
            size_t n = 0;
            int code = 1;
            const bool ok = fetchChunk(fnCopy, cs, chunk, buf.data(), n, &code);

            if (!ok) {
                if (code == 1) {
                    // TEMP failure -> reconnect; if too many, switch seeders
                    logWarn("DL: temp failure chunk %d from seeder %d (worker %d)",
                            chunk, seederPort, i);

                    cs.closeConn();
                    connected = false;
                    consecutiveFailures++;

                    if (consecutiveFailures >= MAX_RETRIES_PER_SEEDER) {
                        logWarn("DL: switching seeder for worker %d (current %d)", i, seederPort);
                        consecutiveFailures = 0;

                        if (!pickNextSeeder()) {
                            logErr("DL: no seeders left for worker %d", i);
                            anyFailed.store(true);
                            if (prog) {
                                prog->failed.store(true);
                                prog->active.store(false);
                                prog->pending.store(false);
                            }
                            break;
                        }

                        seederSwitchCount++;
                        if (seederSwitchCount > MAX_SEEDER_SWITCHES) {
                            logErr("DL: too many seeder switches (worker %d)", i);
                            anyFailed.store(true);
                            if (prog) {
                                prog->failed.store(true);
                                prog->active.store(false);
                                prog->pending.store(false);
                            }
                            break;
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }

                // Permanent failure: keep your current behavior (fail whole download)
                logErr("DL: permanent fail chunk %d from seeder %d code=%d",
                       chunk, seederPort, code);
                anyFailed.store(true);
                if (prog) {
                    prog->failed.store(true);
                    prog->active.store(false);
                    prog->pending.store(false);
                }
                break;
            }

            // Write chunk
            consecutiveFailures = 0;
            if (n > 0) {
                size_t written = std::fwrite(buf.data(), 1, n, out);
                if (written != n) {
                    logErr("DL: write failed for chunk %d", chunk);
                    anyFailed.store(true);
                    if (prog) {
                        prog->failed.store(true);
                        prog->active.store(false);
                    }
                    break;
                }
            }

            if (prog) {
                prog->doneBytes.fetch_add((long long)n);
                prog->doneChunks.fetch_add(1);
            }

            ++chunk;
        }

        std::fclose(out);
        cs.closeConn();

        if (chunk >= r.end && !anyFailed.load()) {
            logInfo("DL: worker %d completed segment", i);
        }
    }));
}

    // Join all threads
    for (size_t i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable()) {
            threads[i].join();
        }
    }

    // Check for failures
    const bool ok = !anyFailed.load() && (!prog || !prog->failed.load());
    if (!ok) {
        logErr("DL incomplete file='%s'", filename.c_str());
        if (prog) {
            prog->failed.store(true);
            prog->active.store(false);
        }
        // Clean up partial files
        for (size_t i = 0; i < partPaths.size(); ++i) {
            std::remove(partPaths[i].c_str());
        }
        return false;
    }

    // Merge parts into final file
    std::remove(outPath.c_str());

    if (parts == 1) {
        // Single part - try rename first, fallback to merge
        if (std::rename(partPaths[0].c_str(), outPath.c_str()) == 0) {
            logInfo("DL: renamed single part to final file");
        } else {
            logWarn("DL: rename failed, using merge");
            if (!mergeParts(outPath, partPaths)) {
                logErr("DL finalize failed (merge) out='%s'", outPath.c_str());
                if (prog) {
                    prog->failed.store(true);
                    prog->active.store(false);
                }
                return false;
            }
            std::remove(partPaths[0].c_str());
        }
    } else {
        // Multiple parts - always merge
        if (!mergeParts(outPath, partPaths)) {
            logErr("DL merge failed out='%s'", outPath.c_str());
            if (prog) {
                prog->failed.store(true);
                prog->active.store(false);
            }
            return false;
        }
        // Clean up parts
        for (size_t i = 0; i < partPaths.size(); ++i) {
            std::remove(partPaths[i].c_str());
        }
    }

    if (prog) {
        prog->success.store(true);
        prog->active.store(false);
        prog->pending.store(false);
    }

    logInfo("DL COMPLETE file='%s' bytes=%lld chunks=%d parts=%d",
            filename.c_str(), fileSize, totalChunks, parts);

    return true;
}

bool ChunkDownloader::probeSize(const std::string& filename,
                               const std::vector<int>& seeders,
                               long long& outSize)
{
    outSize = -1;
    for (size_t i = 0; i < seeders.size(); ++i) {
        if (fetchMeta(filename, seeders[i], outSize))
            return true;
    }
    return false;
}