#include <exception>
#include <iostream>

#include "Application.h"
#include "Logger.h"

int main() {

#ifdef TRACY_ENABLE
    Logger::info("Tracy enabled");
#endif

    try {
        Application app;
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
