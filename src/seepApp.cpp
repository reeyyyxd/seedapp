#include "../inc/seedApp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

SeedApp::SeedApp(int startPort, int endPort, int chunkSize)
: startPort_(startPort),
  endPort_(endPort),
  chunkSize_(chunkSize),
  myPort_(-1),
  server_(chunkSize),
  scanner_(startPort, endPort),
  downloader_(chunkSize, startPort, endPort) {}

int SeedApp::readInt(bool* eof) const {
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) {
        if (eof) *eof = true;  
        return -1;
    }
    if (eof) *eof = false;

    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n') buf[len - 1] = '\0';

    char* end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') return -1;
    return (int)v;
}

void SeedApp::showMenu() const {
    printf("Seed App - Port %d\n", myPort_);
    printf("[1] Download file.\n");
    printf("[2] Download status.\n");
    printf("[3] Exit.\n\n");
    printf("? ");
    fflush(stdout);
}

bool SeedApp::boot() {
    printf("Finding available ports...\n");
    if (!allocator_.claim(startPort_, endPort_)) {
        printf("Connection full, no ports available.\n");
        printf("All ports (%d-%d) are occupied.\n", startPort_, endPort_);
        return false;
    }

    myPort_ = allocator_.port();
    printf("Found port %d.\n", myPort_);
    printf("Listening at port %d.\n\n", myPort_);

    int listenFd = allocator_.takeFd();
    server_.start(myPort_, listenFd);

    return true;
}

void SeedApp::shutdown() {
    server_.stop();
    allocator_.release();
}

void SeedApp::statusFlow() {
    printf("\nSoon to open\n\n");
}

void SeedApp::downloadFlow() {
    printf("\nScanning for available files...\n\n");

    std::vector<FileEntry> files = scanner_.scanOtherPorts(myPort_);
    if (files.empty()) {
        printf("No files available from other ports.\n\n");
        return;
    }

    printf("Available files (from other ports):\n");
    for (int i = 0; i < (int)files.size(); ++i) {
        printf("[%d] %s (%d seeder%s)\n",
               i + 1,
               files[i].filename.c_str(),
               (int)files[i].seeders.size(),
               files[i].seeders.size() > 1 ? "s" : "");
    }
    printf("[0] Back\n\n");
    printf("Select file ID: ");
    fflush(stdout);

    bool eof = false;
    int choice = readInt(&eof);

    if (eof) {
        printf("\nEOF detected. Back to menu.\n\n");
        return;
    }
    if (choice == 0) {
        printf("\nBack to menu.\n\n");
        return;
    }
    if (choice < 1 || choice > (int)files.size()) {
        printf("\nInvalid file ID.\n\n");
        return;
    }

    const FileEntry& selected = files[choice - 1];

    long long remoteSize = -1;
    bool metaOk = downloader_.probeSize(selected.filename, selected.seeders, remoteSize);

    if (scanner_.existsLocal(myPort_, selected.filename)) {
        if (metaOk && remoteSize >= 0) {
            long long localSz = scanner_.localSize(myPort_, selected.filename);

            if (localSz == remoteSize) {
                printf("\nThe file already exists (complete).\n\n");
                return;
            }

            printf("\nFile exists but incomplete/corrupt (local=%lld, remote=%lld). Re-downloading...\n\n",
                   localSz, remoteSize);
            // continue to download ("wb+" to overwrite)
        } else {
            // cant verify size, keep old behavior
            printf("\nThe file already exists.\n\n");
            return;
        }
    }

    printf("\nLocating seeders....Found [%d] Seeder%s.\n",
           (int)selected.seeders.size(),
           selected.seeders.size() > 1 ? "s" : "");

    bool ok = downloader_.download(selected.filename, selected.seeders, myPort_);
    printf(ok ? "Download finished.\n\n" : "Download failed.\n\n");
}

int SeedApp::run() {
    if (!boot()) return 1;

   while (true) {
    showMenu();

    bool eof = false;
    int choice = readInt(&eof);

    if (eof) { 
        printf("\nEOF detected. Exiting...\n");
        break;
    }

    if (choice == 1) downloadFlow();
    else if (choice == 2) statusFlow();
    else if (choice == 3) break;
    else printf("\nInvalid option. Please enter 1, 2, or 3.\n\n");
}

    printf("\nExiting...\n");
    shutdown();
    return 0;
}