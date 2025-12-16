#include "../inc/serversocket.h"
#include "../inc/clientsocket.h"
#include "../inc/logger2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <errno.h>



#define START_PORT 9000
#define END_PORT 9004
#define BUFFER_SIZE 32

std::atomic<bool> serverRunning(true);
int myPort = -1;
int mySocketFd = -1;

int findAvailablePort() {
    for (int port = START_PORT; port <= END_PORT; port++) {
        int test_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (test_fd < 0) {
            continue;
        }

        int opt = 1;
        setsockopt(test_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(test_fd, (struct sockaddr*)&address, sizeof(address)) == 0) {
            myPort = port;
            mySocketFd = test_fd;
            return 0; 
        }

        close(test_fd);
    }

    return -1; 
}

static int readLineFd(int fd, char* out, size_t cap) {
    if (!out || cap == 0) return -1;
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n == 0) { 
            break;
        }
        if (n < 0) {
            if (errno == EINTR) 
            continue;
            return -1;
        }
        out[i++] = c;
        if (c == '\n') 
        break;
    }
    out[i] = '\0';
    return (int)i;
}

static bool recvExact(int fd, void* buf, size_t len) {
    char* p = (char*)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n == 0) 
        return false;

        if (n < 0) {
            if (errno == EINTR)
            continue;

            return false;
        }
        got += (size_t)n;
    }
    return true;
}

static bool sendAll(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
            continue;

            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

static long long fileSizeOf(const char* path) {
    struct stat st;
    if (::stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

void runServer(int port, int server_fd) {
    serversocket ss(port);
    ss.setSocket(server_fd);

    if (ss.listen_only() < 0) {
        logErr("Server thread: Failed to listen on port %d", port);
        return;
    }

    sockaddr_in address{};
    socklen_t addrlen = sizeof(address);

    logInfo("Server thread listening on port %d", port);

    while (serverRunning) {
        int cli = ss.accept(address, addrlen);
        if (cli < 0) {
            if (serverRunning) logErr("Server thread: accept failed on port %d", port);
            continue;
        }

        char line[256];
        int ln = readLineFd(cli, line, sizeof(line));
        if (ln <= 0) {
            logWarn("[Server] failed to read request line");
            ss.close_fd(cli);
            continue;
        }

        size_t L = strlen(line);
        if (L > 0 && line[L - 1] == '\n') line[L - 1] = '\0';

        logInfo("[Server] request: '%s'", line);

    
        if (strncmp(line, "META ", 5) == 0) {
            const char* filename = line + 5;

            char filepath[256];
            snprintf(filepath, sizeof(filepath), "bin/ports/%d/%s", port, filename);

            long long sz = fileSizeOf(filepath);
            if (sz < 0) {
                const char* nf = "<FILE_NOT_FOUND>\n";
                sendAll(cli, nf, strlen(nf));
                logWarn("[Server] META not found: %s", filepath);
                ss.close_fd(cli);
                continue;
            }

            char reply[128];
            snprintf(reply, sizeof(reply), "<META> %lld\n", sz);
            sendAll(cli, reply, strlen(reply));
            logInfo("[Server] META ok: %s size=%lld", filename, sz);
            ss.close_fd(cli);
            continue;
        }

        if (strncmp(line, "GET ", 4) == 0) {
            char filename[200];
            int chunkIndex = -1;

            if (sscanf(line, "GET %199s %d", filename, &chunkIndex) != 2 || chunkIndex < 0) {
                const char* bad = "<BAD_REQUEST>\n";
                sendAll(cli, bad, strlen(bad));
                logWarn("[Server] bad GET request");
                ss.close_fd(cli);
                continue;
            }

            char filepath[256];
            snprintf(filepath, sizeof(filepath), "bin/ports/%d/%s", port, filename);

            long long sz = fileSizeOf(filepath);
            if (sz < 0) {
                const char* nf = "<FILE_NOT_FOUND>\n";
                sendAll(cli, nf, strlen(nf));
                logWarn("[Server] GET not found: %s", filepath);
                ss.close_fd(cli);
                continue;
            }

            long long offset = (long long)chunkIndex * (long long)BUFFER_SIZE;
            if (offset >= sz) {
                const char* re = "<RANGE_ERROR>\n";
                sendAll(cli, re, strlen(re));
                logWarn("[Server] GET out of range: %s chunk=%d", filename, chunkIndex);
                ss.close_fd(cli);
                continue;
            }

            FILE* fp = fopen(filepath, "rb");
            if (!fp) {
                const char* nf = "<FILE_NOT_FOUND>\n";
                sendAll(cli, nf, strlen(nf));
                logWarn("[Server] fopen failed: %s", filepath);
                ss.close_fd(cli);
                continue;
            }

            fseek(fp, offset, SEEK_SET);

            char chunk[BUFFER_SIZE];
            size_t rd = fread(chunk, 1, BUFFER_SIZE, fp);
            fclose(fp);

            char header[128];
            snprintf(header, sizeof(header), "<CHUNK> %d %zu\n", chunkIndex, rd);
            if (!sendAll(cli, header, strlen(header)) || !sendAll(cli, chunk, rd)) {
                logWarn("[Server] send chunk failed: %s chunk=%d", filename, chunkIndex);
                ss.close_fd(cli);
                continue;
            }

            logDbg("[Server] sent chunk %d (%zu bytes) for %s", chunkIndex, rd, filename);
            ss.close_fd(cli);
            continue;
        }

        const char* bad = "<BAD_REQUEST>\n";
        sendAll(cli, bad, strlen(bad));
        ss.close_fd(cli);
    }

    ss.close_fd(ss.fd());
    logInfo("Server thread stopped (port %d)", port);
}


struct FileEntry {
    std::string filename;
    std::vector<int> seeders;
};

static std::string portDir(int port) {
    char path[256];
    snprintf(path, sizeof(path), "bin/ports/%d", port);
    return std::string(path);
}

static bool isRegularFile(const std::string& fullpath) {
    struct stat st;
    if (stat(fullpath.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

static bool fileExistsOnMyPort(int myPort, const std::string& filename) {
    std::string full = portDir(myPort) + "/" + filename;
    struct stat st;
    return (stat(full.c_str(), &st) == 0);
}

static std::vector<FileEntry> scanFilesFromOtherPorts(int myPort) {
    std::vector<FileEntry> out;

    for (int port = START_PORT; port <= END_PORT; port++) {
        if (port == myPort) continue;

        std::string dirPath = portDir(port);
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            std::string fname = entry->d_name;
            std::string fullpath = dirPath + "/" + fname;

            if (!isRegularFile(fullpath)) continue;

            int idx = -1;
            for (int i = 0; i < (int)out.size(); i++) {
                if (out[i].filename == fname) { idx = i; break; }
            }

            if (idx >= 0) {
                bool already = false;
                for (int p : out[idx].seeders) {
                    if (p == port) { already = true; break; }
                }
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

static bool getMetaFromSeeder(const std::string& filename, int seederPort, long long& outSize) {
    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort)) {
        logWarn("META connect failed to port %d", seederPort);
        return false;
    }

    std::string req = "META " + filename + "\n";
    if (!cs.sendData(req)) return false;

    char line[128];
    int n = cs.receiveData(line, sizeof(line));
    if (n <= 0) return false;

    if (strncmp(line, "<FILE_NOT_FOUND>", 15) == 0) return false;

    long long sz = -1;
    if (sscanf(line, "<META> %lld", &sz) == 1 && sz >= 0) {
        outSize = sz;
        return true;
    }
    return false;
}

static bool downloadChunkFromSeeder(const std::string& filename, int seederPort, int chunkIndex,
                                   char* outBuf, size_t& outN) {
    outN = 0;

    clientSocket cs;
    if (!cs.connectServer("127.0.0.1", seederPort)) {
        logWarn("GET connect failed to port %d", seederPort);
        return false;
    }

    char req[256];
    snprintf(req, sizeof(req), "GET %s %d\n", filename.c_str(), chunkIndex);
    if (!cs.sendData(std::string(req))) return false;

    char header[128];
    int hn = cs.receiveData(header, sizeof(header));
    if (hn <= 0) return false;

    if (strncmp(header, "<FILE_NOT_FOUND>", 15) == 0) return false;
    if (strncmp(header, "<RANGE_ERROR>", 12) == 0) return false;
    if (strncmp(header, "<BAD_REQUEST>", 13) == 0) return false;

    int idx = -1;
    int nbytes = 0;
    if (sscanf(header, "<CHUNK> %d %d", &idx, &nbytes) != 2 || idx != chunkIndex || nbytes < 0 || nbytes > BUFFER_SIZE) {
        logWarn("bad chunk header from port %d: %s", seederPort, header);
        return false;
    }

    char tmp[BUFFER_SIZE + 1];
    int rn = cs.receiveData(tmp, (size_t)nbytes + 1);
    if (rn < nbytes) {
        logWarn("partial chunk read: got=%d expected=%d (port %d)", rn, nbytes, seederPort);
        return false;
    }

    memcpy(outBuf, tmp, (size_t)nbytes);
    outN = (size_t)nbytes;
    return true;
}

static bool downloadFileChunked(const std::string& filename, const std::vector<int>& seeders, int myPort) {
    if (seeders.empty()) return false;

    long long size = -1;
    bool metaOk = false;
    for (size_t i = 0; i < seeders.size(); i++) {
        if (getMetaFromSeeder(filename, seeders[i], size)) {
            metaOk = true;
            break;
        }
    }
    if (!metaOk || size < 0) {
        logErr("META failed for %s", filename.c_str());
        return false;
    }

    int totalChunks = (int)((size + (BUFFER_SIZE - 1)) / BUFFER_SIZE);
    logInfo("Downloading '%s' size=%lld chunks=%d seeders=%d",
            filename.c_str(), size, totalChunks, (int)seeders.size());

    std::string outPath = portDir(myPort) + "/" + filename;
    FILE* out = fopen(outPath.c_str(), "wb+");
    if (!out) {
        logErr("failed to open output file: %s", outPath.c_str());
        return false;
    }

    for (int chunk = 0; chunk < totalChunks; chunk++) {
        bool got = false;

        for (size_t attempt = 0; attempt < seeders.size(); attempt++) {
            int seederPort = seeders[(chunk + (int)attempt) % seeders.size()];

            char buf[BUFFER_SIZE];
            size_t n = 0;

            if (downloadChunkFromSeeder(filename, seederPort, chunk, buf, n)) {
                long long offset = (long long)chunk * BUFFER_SIZE;
                fseek(out, offset, SEEK_SET);
                fwrite(buf, 1, n, out);
                got = true;

                // progress
                int pct = (int)(((long long)(chunk + 1) * 100) / totalChunks);
                logInfo("chunk %d/%d ok from port %d (%zu bytes) [%d%%]",
                        chunk + 1, totalChunks, seederPort, n, pct);
                break;
            }
        }

        if (!got) {
            logErr("failed to download chunk %d for %s", chunk, filename.c_str());
            fclose(out);
            return false;
        }
    }

    fclose(out);
    logInfo("Download complete: %s", outPath.c_str());
    return true;
}

void handleDownloadFile() {
    printf("\nScanning for available files...\n\n");

    auto files = scanFilesFromOtherPorts(myPort);

    if (files.empty()) {
        printf("No files available from other ports.\n\n");
        return;
    }

    printf("Available files (from other ports):\n");
    for (int i = 0; i < (int)files.size(); i++) {
        printf("[%d] %s (%d seeder%s)\n",
               i + 1,
               files[i].filename.c_str(),
               (int)files[i].seeders.size(),
               files[i].seeders.size() > 1 ? "s" : "");
    }
    printf("[0] Back\n\n");
    printf("Select file ID: ");
    fflush(stdout);

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) {
        printf("\nInput error.\n\n");
        return;
    }

    int choice = atoi(buf);
    if (choice == 0) {
        printf("\nBack to menu.\n\n");
        return;
    }

    if (choice < 1 || choice > (int)files.size()) {
        printf("\nInvalid file ID.\n\n");
        return;
    }

    const auto& selected = files[choice - 1];

    if (fileExistsOnMyPort(myPort, selected.filename)) {
        printf("\nThe file already exists.\n\n");
        return;
    }

    // downlod later-ish???
    printf("\nLocating seeders....Found [%d] Seeder%s.\n",
           (int)selected.seeders.size(),
           selected.seeders.size() > 1 ? "s" : "");

    if (!downloadFileChunked(selected.filename, selected.seeders, myPort)) {
    printf("Download failed.\n\n");
    } else {
        printf("Download finished.\n\n");
    }

}

void handleDownloadStatus() {
    printf("\nSoon to open\n\n");
}

int main() {
    printf("Finding available ports...\n");

    if (findAvailablePort() < 0) {
        printf("Connection full, no ports available.\n");
        printf("All ports (9000-9004) are occupied.\n");
        return 1;
    }

    printf("Found port %d.\n", myPort);
    printf("Listening at port %d.\n\n", myPort);

    std::thread serverThread(runServer, myPort, mySocketFd);
    serverThread.detach();

    usleep(100000);

    char buffer[128];
    while (1) {
        printf("Seed App - Port %d\n", myPort);
        printf("[1] Download file.\n");
        printf("[2] Download status.\n");
        printf("[3] Exit.\n\n");
        printf("? ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("\nInput error or EOF. Exiting...\n");
            break;
        }

        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        char* end = NULL;
        long choice = strtol(buffer, &end, 10);

        if (end == buffer || *end != '\0') {
            printf("\nInvalid input. Please enter 1, 2, or 3.\n\n");
            continue;
        }

        switch ((int)choice) {
            case 1:
                handleDownloadFile();
                break;
            case 2:
                handleDownloadStatus();
                break;
            case 3:
                printf("\nExiting...\n");
                serverRunning = false;
                usleep(200000);
                if (mySocketFd >= 0) {
                    close(mySocketFd);
                }
                return 0;
            default:
                printf("\nInvalid option. Please enter 1, 2, or 3.\n\n");
                break;
        }
    }

    serverRunning = false;
    usleep(200000);
    if (mySocketFd >= 0) {
        close(mySocketFd);
    }
    return 0;
}