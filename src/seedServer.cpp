#include "../inc/seedServer.h"
#include "../inc/netIO.h"
#include "../inc/serversocket.h"
#include "../inc/logger2.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>

SeedServer::SeedServer(int chunkSize)
: running_(false), chunkSize_(chunkSize), listenFd_(-1) {}

SeedServer::~SeedServer() { stop(); }

bool SeedServer::start(int port, int boundListenFd) {
    if (running_) return true;

    listenFd_ = boundListenFd;
    running_ = true;

    thread_ = std::thread(&SeedServer::serveLoop, this, port, boundListenFd);
    return true;
}

void SeedServer::stop() {
    if (!running_) return;

    running_ = false;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }

    if (thread_.joinable()) thread_.join();
}

long long SeedServer::getFileSizeBytes(const char* path) {
    struct stat st;
    if (::stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

bool SeedServer::handleList(int clientFd, int port) {
    const char* begin = "<LIST>\n";
    if (!NetIo::sendAll(clientFd, begin, strlen(begin))) return false;

    char dirPath[256];
    snprintf(dirPath, sizeof(dirPath), "bin/ports/%d", port);

    DIR* dir = opendir(dirPath);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dirPath, entry->d_name);

            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (!S_ISREG(st.st_mode)) continue;

            char row[512];
            snprintf(row, sizeof(row), "FILE %s\n", entry->d_name);
            if (!NetIo::sendAll(clientFd, row, strlen(row))) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
    }

    const char* end = "<END>\n";
    return NetIo::sendAll(clientFd, end, strlen(end));
}

bool SeedServer::handleMeta(int clientFd, int port, const char* filename) {
    char path[256];
    snprintf(path, sizeof(path), "bin/ports/%d/%s", port, filename);

    long long sz = getFileSizeBytes(path);
    if (sz < 0) {
        const char* nf = "<FILE_NOT_FOUND>\n";
        NetIo::sendAll(clientFd, nf, strlen(nf));
        return false;
    }

    char reply[128];
    snprintf(reply, sizeof(reply), "<META> %lld\n", sz);
    return NetIo::sendAll(clientFd, reply, strlen(reply));
}

bool SeedServer::handleGet(int clientFd, int port, const char* line) {
    char filename[200];
    int chunkIndex = -1;

    if (sscanf(line, "GET %199s %d", filename, &chunkIndex) != 2 || chunkIndex < 0) {
        const char* bad = "<BAD_REQUEST>\n";
        NetIo::sendAll(clientFd, bad, strlen(bad));
        return false;
    }

    char path[256];
    snprintf(path, sizeof(path), "bin/ports/%d/%s", port, filename);

    long long sz = getFileSizeBytes(path);
    if (sz < 0) {
        const char* nf = "<FILE_NOT_FOUND>\n";
        NetIo::sendAll(clientFd, nf, strlen(nf));
        return false;
    }

    long long offset = (long long)chunkIndex * (long long)chunkSize_;
    if (offset >= sz) {
        const char* re = "<RANGE_ERROR>\n";
        NetIo::sendAll(clientFd, re, strlen(re));
        return false;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        const char* nf = "<FILE_NOT_FOUND>\n";
        NetIo::sendAll(clientFd, nf, strlen(nf));
        return false;
    }

    fseek(fp, offset, SEEK_SET);

    std::string buf(chunkSize_, '\0');
    size_t rd = fread(&buf[0], 1, (size_t)chunkSize_, fp);
    fclose(fp);

    char header[128];
    snprintf(header, sizeof(header), "<CHUNK> %d %zu\n", chunkIndex, rd);
    if (!NetIo::sendAll(clientFd, header, strlen(header))) return false;
    return NetIo::sendAll(clientFd, buf.data(), rd);
}

void SeedServer::handleClient(int clientFd, int port) {
    char line[256];
    int n = NetIo::recvLine(clientFd, line, sizeof(line));
    if (n <= 0) { ::close(clientFd); return; }

    size_t L = strlen(line);
    if (L && line[L - 1] == '\n') line[L - 1] = '\0';

    if (strcmp(line, "LIST") == 0) {
        handleList(clientFd, port);
        ::close(clientFd);
        return;
    }

    if (strncmp(line, "META ", 5) == 0) {
        handleMeta(clientFd, port, line + 5);
        ::close(clientFd);
        return;
    }

    if (strncmp(line, "GET ", 4) == 0) {
        handleGet(clientFd, port, line);
        ::close(clientFd);
        return;
    }

    const char* bad = "<BAD_REQUEST>\n";
    NetIo::sendAll(clientFd, bad, strlen(bad));
    ::close(clientFd);
}

void SeedServer::serveLoop(int port, int listenFd) {
    serversocket ss(port);
    ss.setSocket(listenFd);

    if (ss.listen_only() < 0) {
        logErr("SeedServer listen failed on port %d", port);
        running_ = false;
        return;
    }

    logInfo("SeedServer listening on port %d", port);

    sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);

    while (running_) {
        int clientFd = ss.accept(addr, addrlen);

        if (clientFd < 0) {
            if (!running_) break;
            continue;
        }

        // ✅ multithread AFTER accept
        std::thread(&SeedServer::handleClient, this, clientFd, port).detach();
    }

    logInfo("SeedServer stopped (port %d)", port);
}