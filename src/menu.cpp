#include "../inc/seedApp.h"
#include "../inc/logger2.h"

#define START_PORT   9000
#define END_PORT     9004
#define BUFFER_SIZE  32

int main() {
    Logger::init("SeedApp", Logger::INFO, Logger::TRACE, "logs");
    SeedApp app(START_PORT, END_PORT, BUFFER_SIZE);
    return app.run();
}