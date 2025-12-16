#include "../inc/logger2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "portAllocator.h"
#include "seedServer.h"
#include "fileScanner.h"
#include "chunkDownloader.h"

#define START_PORT   9000
#define END_PORT     9004
#define BUFFER_SIZE  32

static int readChoiceInt() {
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;

    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n') buf[len - 1] = '\0';

    char* end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') return -1;
    return (int)v;
}

static void showMenu(int myPort) {
    printf("Seed App - Port %d\n", myPort);
    printf("[1] Download file.\n");
    printf("[2] Download status.\n");
    printf("[3] Exit.\n\n");
    printf("? ");
    fflush(stdout);
}

static void downloadFileFlow(int myPort, FileScanner& scanner, ChunkDownloader& downloader) {
    printf("\nScanning for available files...\n\n");

    std::vector<FileEntry> files = scanner.scanOtherPorts(myPort);
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

    int choice = readChoiceInt();
    if (choice == 0) {
        printf("\nBack to menu.\n\n");
        return;
    }
    if (choice < 1 || choice > (int)files.size()) {
        printf("\nInvalid file ID.\n\n");
        return;
    }

    const FileEntry& selected = files[choice - 1];

    if (scanner.existsLocal(myPort, selected.filename)) {
        printf("\nThe file already exists.\n\n");
        return;
    }

    printf("\nLocating seeders....Found [%d] Seeder%s.\n",
           (int)selected.seeders.size(),
           selected.seeders.size() > 1 ? "s" : "");

    bool ok = downloader.download(selected.filename, selected.seeders, myPort);
    if (!ok) {
        printf("Download failed.\n\n");
    } else {
        printf("Download finished.\n\n");
    }
}

static void downloadStatusFlow() {
    printf("\nSoon to open\n\n");
}

int main() {
    printf("Finding available ports...\n");

    PortAllocator allocator;
    if (!allocator.claim(START_PORT, END_PORT)) {
        printf("Connection full, no ports available.\n");
        printf("All ports (9000-9004) are occupied.\n");
        return 1;
    }

    int myPort = allocator.port();
    printf("Found port %d.\n", myPort);
    printf("Listening at port %d.\n\n", myPort);

    SeedServer server(BUFFER_SIZE);
    server.start(myPort, allocator.fd());

    // give server to start listening
    usleep(100000);

    FileScanner scanner(START_PORT, END_PORT);
    ChunkDownloader downloader(BUFFER_SIZE, START_PORT, END_PORT);

    while (1) {
        showMenu(myPort);

        int choice = readChoiceInt();
        if (choice < 0) {
            printf("\nInvalid input. Please enter 1, 2, or 3.\n\n");
            continue;
        }

        switch (choice) {
            case 1:
                downloadFileFlow(myPort, scanner, downloader);
                break;
            case 2:
                downloadStatusFlow();
                break;
            case 3:
                printf("\nExiting...\n");
                server.stop();
                allocator.release();
                return 0;
            default:
                printf("\nInvalid option. Please enter 1, 2, or 3.\n\n");
                break;
        }
    }

    server.stop();
    allocator.release();
    return 0;
}