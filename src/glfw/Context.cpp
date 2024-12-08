#include "Context.h"

#include <iostream>
#include <GLFW/glfw3.h>

namespace glfw {
    void Context::defaultErrorCallback(int error, const char *description) {
        std::cerr << "GLFW error " << std::format("{:#010x}", error) << ": " << description << std::endl;
    }

    Context::Context() {
        if (isInitialized) {
            throw std::runtime_error("GLFW is already initialized");
        }
        glfwSetErrorCallback(defaultErrorCallback);
        if (!glfwInit()) {
            throw std::runtime_error("GLFW initialization failed");
        }
        isInitialized = true;

        if (!glfwVulkanSupported()) {
            throw std::runtime_error("GLFW vulkan not supported");
        }
    }

    Context::~Context() {
        glfwTerminate();
        isInitialized = false;
    }

    std::vector<const char *> Context::getRequiredInstanceExtensions() const {
        uint32_t count;
        const char **extensions = glfwGetRequiredInstanceExtensions(&count);
        return std::vector(extensions, extensions + count);
    }
}
