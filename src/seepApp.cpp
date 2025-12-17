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

int SeedApp::readInt() const {
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;

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

    server_.start(myPort_, allocator_.fd());
    usleep(100000);
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

    int choice = readInt();
    if (choice == 0) { printf("\nBack to menu.\n\n"); return; }
    if (choice < 1 || choice > (int)files.size()) { printf("\nInvalid file ID.\n\n"); return; }

    const FileEntry& selected = files[choice - 1];

    if (scanner_.existsLocal(myPort_, selected.filename)) {
        printf("\nThe file already exists.\n\n");
        return;
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
        int choice = readInt();

        if (choice == 1) downloadFlow();
        else if (choice == 2) statusFlow();
        else if (choice == 3) break;
        else printf("\nInvalid option. Please enter 1, 2, or 3.\n\n");
    }

    printf("\nExiting...\n");
    shutdown();
    return 0;
}