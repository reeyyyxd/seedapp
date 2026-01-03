#ifndef __CHUNCKDOWNLOADER_H__
#define __CHUNCKDOWNLOADER_H__

#include <string>
#include <vector>
#include <atomic>


enum class FetchCode {
    OK = 0,
    TEMP_FAIL,
    FILE_NOT_FOUND,
    RANGE_OR_BAD
};


struct DownloadProgress {
    std::atomic<long long> totalBytes{0};
    std::atomic<long long> doneBytes{0};
    std::atomic<int> totalChunks{0};
    std::atomic<int> doneChunks{0};

    std::atomic<bool> active{false};
    std::atomic<bool> pending{false};  
    std::atomic<bool> success{false};
    std::atomic<bool> failed{false};

    void reset() {
        totalBytes.store(0);
        doneBytes.store(0);
        totalChunks.store(0);
        doneChunks.store(0);
        active.store(false);
        pending.store(false);   
        success.store(false);
        failed.store(false);
    }
};

class ChunkDownloader {
public:
    ChunkDownloader(int chunkSize, int startPort, int endPort);

    bool download(const std::string& filename,
                  const std::vector<int>& seeders,
                  int myPort);

    bool download(const std::string& filename,
                  const std::vector<int>& seeders,
                  int myPort,
                  DownloadProgress* prog);

    bool probeSize(const std::string& filename,
                   const std::vector<int>& seeders,
                   long long& outSize);

private:
    bool fetchMeta(const std::string& filename, int seederPort, long long& outSize);
    bool fetchChunk(const std::string& filename, int seederPort, int chunkIndex,
                char* outBuf, size_t& outN, int* outCode);

    std::string portDirectory(int port) const;

    int chunkSize_;
    int startPort_;
    int endPort_;
};

#endif