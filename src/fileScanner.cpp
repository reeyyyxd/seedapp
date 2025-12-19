#include "../inc/fileScanner.h"
#include "../inc/clientsocket.h"
#include "../inc/logger2.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

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

static void stripNewlines(char* s) {
    if (!s) return;
    size_t L = strlen(s);
    while (L > 0 && (s[L - 1] == '\n' || s[L - 1] == '\r')) {
        s[L - 1] = '\0';
        --L;
    }
}

static bool listFilesFromPort(int port, std::vector<std::string>& outNames) {
    outNames.clear();

    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", port)) {
        return false;
    }

    if (!cs.sendData("LIST\n")) {
        return false;
    }

    char line[512];

    int n = cs.receiveLine(line, sizeof(line));
    if (n <= 0) return false;
    stripNewlines(line);

    if (strcmp(line, "<LIST>") != 0) {
        logWarn("LIST: unexpected first line from port %d: '%s'", port, line);
        return false;
    }

    while (true) {
        n = cs.receiveLine(line, sizeof(line));
        if (n <= 0) return false;
        stripNewlines(line);

        if (strcmp(line, "<END>") == 0) break;

        if (strncmp(line, "FILE ", 5) == 0) {
            const char* name = line + 5;
            if (*name) outNames.push_back(std::string(name));
        }
    }

    return true;
}

std::vector<FileEntry> FileScanner::scanOtherPorts(int myPort) const {
    std::vector<FileEntry> out;

    for (int port = startPort_; port <= endPort_; ++port) {
        if (port == myPort) continue;

        //if port inactive or unsupported list
        std::vector<std::string> names;
        if (!listFilesFromPort(port, names)) {
            continue; 
        }

        for (size_t k = 0; k < names.size(); ++k) {
            const std::string& fname = names[k];

            int idx = -1;
            for (int i = 0; i < (int)out.size(); ++i) {
                if (out[i].filename == fname) { idx = i; break; }
            }

            if (idx >= 0) {
                bool already = false;
                for (size_t j = 0; j < out[idx].seeders.size(); ++j) {
                    if (out[idx].seeders[j] == port) { already = true; break; }
                }
                if (!already) out[idx].seeders.push_back(port);
            } else {
                FileEntry fe;
                fe.filename = fname;
                fe.seeders.push_back(port);
                out.push_back(fe);
            }
        }
    }

    return out;
}

long long FileScanner::localSize(int myPort, const std::string& filename) const {
    std::string full = portDirectory(myPort) + "/" + filename;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;
    return (long long)st.st_size;
}

bool FileScanner::existsLocal(int myPort, const std::string& filename, long long expectedSize) const {
    long long sz = localSize(myPort, filename);
    return (sz >= 0 && sz == expectedSize);
}