#ifndef __CHUNCKDOWNLOADER_H__
#define __CHUNCKDOWNLOADER_H__

#include <string>
#include <vector>

class ChunkDownloader {
public:
    ChunkDownloader(int chunkSize, int startPort, int endPort);

    bool download(const std::string& filename,
                  const std::vector<int>& seeders,
                  int myPort);

private:
    bool fetchMeta(const std::string& filename, int seederPort, long long& outSize);
    bool fetchChunk(const std::string& filename, int seederPort, int chunkIndex,
                    char* outBuf, size_t& outN);

    std::string portDirectory(int port) const;

    int chunkSize_;
    int startPort_;
    int endPort_;
};
#endif