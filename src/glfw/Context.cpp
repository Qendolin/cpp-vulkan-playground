#include "Context.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <utility>

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
        primary = true;

        if (!glfwVulkanSupported()) {
            throw std::runtime_error("GLFW vulkan not supported");
        }
    }

    Context::Context(Context &&other) noexcept { this->primary = std::exchange(other.primary, false); }

    Context::~Context() {
        if (!primary)
            return;
        glfwTerminate();
        isInitialized = false;
    }

    std::vector<const char *> Context::getRequiredInstanceExtensions() {
        uint32_t count;
        const char **extensions = glfwGetRequiredInstanceExtensions(&count);
        return {extensions, extensions + count};
    }
} // namespace glfw
