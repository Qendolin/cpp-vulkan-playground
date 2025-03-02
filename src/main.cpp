#include <exception>
#include <iostream>

#include "Application.h"
#include "GraphicsBackend.h"
#include "Logger.h"

int main() {

#ifdef TRACY_ENABLE
    Logger::info("Tracy enabled");
#endif

    try {
        AppContext ctx({.width = 1600, .height = 900, .title = "Vulkan Playground"});
        Application app(ctx);
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
