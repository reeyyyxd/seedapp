#include "../inc/fileScanner.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

FileScanner::FileScanner(int startPort, int endPort)
: startPort_(startPort), endPort_(endPort) {}

std::string FileScanner::portDirectory(int port) const {
    char path[256];
    snprintf(path, sizeof(path), "bin/ports/%d", port);
    return std::string(path);
}

bool FileScanner::isRegularFile(const std::string& path) const {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool FileScanner::existsLocal(int myPort, const std::string& filename) const {
    std::string full = portDirectory(myPort) + "/" + filename;
    struct stat st;
    return stat(full.c_str(), &st) == 0;
}

std::vector<FileEntry> FileScanner::scanOtherPorts(int myPort) const {
    std::vector<FileEntry> out;

    for (int port = startPort_; port <= endPort_; ++port) {
        if (port == myPort) continue;

        std::string dirPath = portDirectory(port);
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) continue;

        dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

            std::string fname = entry->d_name;
            std::string fullpath = dirPath + "/" + fname;

            if (!isRegularFile(fullpath)) continue;

            int idx = -1;
            for (int i = 0; i < (int)out.size(); ++i) {
                if (out[i].filename == fname) { idx = i; break; }
            }

            if (idx >= 0) {
                bool already = false;
                for (int p : out[idx].seeders) if (p == port) { already = true; break; }
                if (!already) out[idx].seeders.push_back(port);
            } else {
                FileEntry fe;
                fe.filename = fname;
                fe.seeders.push_back(port);
                out.push_back(fe);
            }
        }
        closedir(dir);
    }

    return out;
}