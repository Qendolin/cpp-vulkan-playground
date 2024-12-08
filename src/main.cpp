#include <exception>
#include <iostream>

#include "Application.h"

#ifdef _WIN32
extern "C" {
// signals to the system that the dedicated gpu should be used
__declspec(dllexport) uint32_t NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {
    try {
        Application app;
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
