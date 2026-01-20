#include "../inc/seedApp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cerrno>
#include <cstdlib>


static std::string dQuote(const std::string& s) {
    std::string out; out.reserve(s.size()+2);
    out.push_back('"');
    for (char c : s) out += (c=='"') ? "\\\"" : std::string(1,c);
    out.push_back('"');
    return out;
}

static void scriptRunner(const std::string& targetPath)
{
    const std::string shell = "powershell";
    const std::string script = "script.ps1";

    std::string cmd = shell
        + " -NoProfile -ExecutionPolicy Bypass -File "
        + dQuote(script)
        + " -Path "
        + dQuote(targetPath);

    (void)std::system(cmd.c_str());
}


static std::string fmtElapsed(std::chrono::steady_clock::duration d) {
    using namespace std::chrono;
    long long sec = duration_cast<seconds>(d).count();
    if (sec < 0) sec = 0;

    long long h = sec / 3600;
    long long m = (sec % 3600) / 60;
    long long s = sec % 60;

    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02lld:%02lld", m, s);
    return std::string(buf);
}

//this for fancy2x
static void clearScreen() {
    printf("\033[H\033[J");
    fflush(stdout);
}

static std::string fmtBar(double pct, int width = 30) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    int filled = (int)((pct / 100.0) * width + 0.5);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;

    std::string bar;
    bar.reserve(width + 2);
    bar.push_back('[');
    bar.append(filled, '#');              // or '█' if your terminal supports it
    bar.append(width - filled, ' ');
    bar.push_back(']');
    return bar;
}

static double safePct(long long done, long long total) {
    if (total <= 0) return 0.0;
    return (100.0 * (double)done / (double)total);
}



static struct termios origTerm;

static void enableRawMode() {
    tcgetattr(STDIN_FILENO, &origTerm);
    struct termios raw = origTerm;
    raw.c_lflag &= ~(ICANON | ECHO); 
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTerm);
}



static bool keyPressed(char& out) {
    fd_set set;
    struct timeval timeout{0, 0};

    while (true) {
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        int r = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
        if (r > 0) {
            ssize_t n = read(STDIN_FILENO, &out, 1);
            return n == 1;
        }
        if (r == 0) {
            return false; // no key
        }
        if (errno == EINTR) {
            continue; // interrupted by a signal; try again
        }
        // Other error: treat as no key or log it
        return false;
    }
}


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

    for (;;) {
        errno = 0;
        if (fgets(buf, sizeof(buf), stdin)) {
            if (eof) *eof = false;
            break; // got a line
        }

        if (errno == EINTR) {
            clearerr(stdin);
            continue;
        }
        if (feof(stdin)) {
            if (eof) *eof = true;
        } else {
            //perror("fgets");
        }
        return -1;
    }

    // Strip newline
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n') buf[len - 1] = '\0';

    char* end = nullptr;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') return -1;

    return static_cast<int>(v);
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
    enableRawMode();


    while (true) {
        clearScreen();
        printf("Download Status\n");
        printf("Press [0] to return to menu\n\n");

        {
            std::lock_guard<std::mutex> lock(jobsMu_);
            if (jobs_.empty()) {
                printf("No download activity.\n\n");
            } else {
                for (size_t i = 0; i < jobs_.size(); ++i) {
                    auto& j = jobs_[i];

                    long long total   = j->progress.totalBytes.load();
                    long long done    = j->progress.doneBytes.load();
                    int tChunks       = j->progress.totalChunks.load();
                    int dChunks       = j->progress.doneChunks.load();

                    double pct = safePct(done, total);
                    std::string bar = fmtBar(pct, 30);

                    auto now = std::chrono::steady_clock::now();
                    double dt = std::chrono::duration<double>(now - j->lastTick).count();
                    long long delta = done - j->lastDoneBytes;
                    double speed = (dt > 0.0) ? ((double)delta / dt) / 1024.0 : 0.0;
                    j->lastTick = now;
                    j->lastDoneBytes = done;
                    auto endTime = j->finished.load() ? j->end : now;
                    auto elapsed = endTime - j->start;

           

                    printf("[%zu] %s\n", i + 1, j->filename.c_str());
                    printf(" %s  %6.2f%%\n", bar.c_str(), pct);
                    printf(" Chunks   : %d / %d\n", dChunks, tChunks);
                    printf(" Speed    : %.2f KB/s\n", speed);
                    if (j->finished.load()) {
                        printf(" Time Completed    : %s\n", fmtElapsed(elapsed).c_str());
                    } else {
                        printf(" Time Elapsed  : %s\n", fmtElapsed(elapsed).c_str());
                    }

                    if (j->progress.success)
                        printf(" State    : COMPLETED\n");
                    else if (j->progress.failed)
                        printf(" State    : FAILED\n");
                    else if (j->progress.pending)
                        printf(" State    : LOOKING FOR SEEDERS...\n");
                    else if (j->progress.active)
                        printf(" State    : DOWNLOADING\n");

                    printf("\n");
                }
            }
        }

        fflush(stdout);

        for (int i = 0; i < 30; ++i) {
            char c;
            if (keyPressed(c) && c == '0') {
                disableRawMode();
                clearScreen();
                fflush(stdout);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SeedApp::downloadFlow() {
    printf("\nScanning for available files...\n\n");

    std::vector<FileEntry> files = scanner_.scanOtherPorts(myPort_);
    if (files.empty()) {
        printf("No files available from other ports.\n\n");
        return;
    }

    printf("Available files (from other ports):\n");
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        printf("[%d] %s (%d seeder%s)\n",
               i + 1,
               files[i].filename.c_str(),
               static_cast<int>(files[i].seeders.size()),
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

    if (choice < 1 || choice > static_cast<int>(files.size())) {
        printf("\nInvalid file ID.\n\n");
        return;
    }

    FileEntry selected = files[choice - 1];

    long long remoteSize = -1;
    bool metaOk = downloader_.probeSize(
        selected.filename,
        selected.seeders,
        remoteSize
    );

    if (scanner_.existsLocal(myPort_, selected.filename)) {
        if (metaOk && remoteSize >= 0) {
            long long localSz = scanner_.localSize(myPort_, selected.filename);

            if (localSz == remoteSize) {
                printf("\nThe file already exists (complete).\n\n");
                return;
            }

            printf(
                "\nFile exists but incomplete/corrupt "
                "(local=%lld, remote=%lld). Re-downloading...\n\n",
                localSz,
                remoteSize
            );
        } else {
            printf("\nThe file already exists.\n\n");
            return;
        }
    }

    printf(
        "\nLocating seeders....Found [%d] Seeder%s.\n",
        static_cast<int>(selected.seeders.size()),
        selected.seeders.size() > 1 ? "s" : ""
    );

    // auto job = std::make_unique<DownloadJob>();
    // job->filename = selected.filename;
    // job->seeders  = selected.seeders;
    // job->start    = std::chrono::steady_clock::now();
    // job->lastDoneBytes = 0;
    // job->lastTick = job->start;
    // job->progress.reset();
    // job->progress.active.store(true);

    
    // job->worker = std::thread([this, j = job.get()] {
    //     bool ok = downloader_.download(
    //         j->filename,
    //         j->seeders,
    //         myPort_,
    //         &j->progress
    //     );

    //     if (ok) {
    //         j->progress.success.store(true);
    //         j->progress.active.store(false);
    //        // scriptRunner("bin/ports");

    //     } else {
    //         j->progress.failed.store(true);
    //         j->progress.active.store(false);
    //     }
    // });


    std::unique_ptr<DownloadJob> job(new DownloadJob());
        job->filename = selected.filename;
        job->seeders  = selected.seeders;
        job->start    = std::chrono::steady_clock::now();
        job->end      = job->start; 
        job->finished.store(false);    
        job->lastDoneBytes = 0;
        job->lastTick = job->start;
        job->progress.reset();
        job->progress.active.store(true);

        DownloadJob* j = job.get();

        job->worker = std::thread([this, j] {
            bool ok = downloader_.download(
                j->filename,
                j->seeders,
                myPort_,
                &j->progress
            );

            // Always update state flags
            j->progress.active.store(false);

            if (ok) {
                j->progress.success.store(true);
                j->progress.failed.store(false);
                 // scriptRunner("bin/ports");
            } else {
                j->progress.success.store(false);
                j->progress.failed.store(true);
            }

            j->end = std::chrono::steady_clock::now();
            j->finished.store(true);
        });


    job->worker.detach();

    {
        std::lock_guard<std::mutex> lock(jobsMu_);
        jobs_.push_back(std::move(job));
    }

    printf("\nDownload started in background.\n\n");
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

        if (choice == 1)
            downloadFlow();
        else if (choice == 2)
            statusFlow();
        else if (choice == 3)
            break;
        else
            printf("\nInvalid option. Please enter 1, 2, or 3.\n\n");
    }

    printf("\nExiting...\n");
    shutdown();
    return 0;
}