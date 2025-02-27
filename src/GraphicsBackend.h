#pragma once

#include <set>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan.hpp>

#include "Swapchain.h"
#include "glfw/Context.h"
#include "glfw/Input.h"
#include "glfw/Window.h"


class Swapchain;

class InstanceContext {
public:
    glfw::Context glfw = {};
    vk::UniqueInstance instance = {};
    vk::UniqueDebugUtilsMessengerEXT debugMessenger = {};

    std::set<std::string> supportedExtensions;

    InstanceContext();

    [[nodiscard]] vk::Instance get() const { return *instance; }

    InstanceContext(InstanceContext &&other) = delete;

    InstanceContext &operator=(InstanceContext &&other) = delete;

    InstanceContext(const InstanceContext &other) = delete;

    InstanceContext &operator=(const InstanceContext &other) = delete;
};

class DeviceContext {
public:
    InstanceContext instace;

    vk::PhysicalDevice physicalDevice = {};
    vk::UniqueDevice device = {};

    // graphics and compute
    uint32_t mainQueueFamily = -1u;
    // async compute, if available
    uint32_t computeQueueFamily = -1u;
    // async transfer, if available
    uint32_t transferQueueFamily = -1u;

    vk::Queue mainQueue = {};
    vk::Queue computeQueue = {};
    vk::Queue transferQueue = {};

    vma::UniqueAllocator allocator = {};

    std::set<std::string> supportedExtensions;

    DeviceContext();

    [[nodiscard]] vk::Device get() const { return *device; }

    DeviceContext(DeviceContext &&other) = delete;

    DeviceContext &operator=(DeviceContext &&other) = delete;

    DeviceContext(const DeviceContext &other) = delete;

    DeviceContext &operator=(const DeviceContext &other) = delete;
};

class WindowContext {
public:
    struct Config {
        int width;
        int height;
        std::string title;
    };

    DeviceContext device;
    const InstanceContext &instance;

    glfw::UniqueWindow window;
    vk::UniqueSurfaceKHR surface;

    std::unique_ptr<glfw::Input> input;

    explicit WindowContext(const Config &config);

    [[nodiscard]] glfw::Window get() const { return *window; }

    WindowContext(WindowContext &&other) = delete;

    WindowContext &operator=(WindowContext &&other) noexcept = delete;

    WindowContext(const WindowContext &other) = delete;

    WindowContext &operator=(const WindowContext &other) = delete;
};

class AppContext {
public:
    WindowContext window;
    const DeviceContext &device;
    const InstanceContext &instance;

    std::unique_ptr<Swapchain> swapchain;

    explicit AppContext(const WindowContext::Config &window_config);

    AppContext(AppContext &&other) = delete;

    AppContext &operator=(AppContext &&other) noexcept = delete;

    AppContext(const AppContext &other) = delete;

    AppContext &operator=(const AppContext &other) = delete;
};
