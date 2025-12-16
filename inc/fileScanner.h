#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H

#include <string>
#include <vector>


struct FileEntry {
    std::string filename;
    std::vector<int> seeders;
};

class FileScanner {
public:
    FileScanner(int startPort, int endPort);

    std::vector<FileEntry> scanOtherPorts(int myPort) const;
    bool existsLocal(int myPort, const std::string& filename) const;

private:
    std::string portDirectory(int port) const;
    bool isRegularFile(const std::string& path) const;

    int startPort_;
    int endPort_;
};

#endif