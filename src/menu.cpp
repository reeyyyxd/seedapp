#include "../inc/seedApp.h"

#define START_PORT   9000
#define END_PORT     9004
#define BUFFER_SIZE  32

int main() {
    SeedApp app(START_PORT, END_PORT, BUFFER_SIZE);
    return app.run();
}