#pragma once

#include <ranges>

#include <vulkan/vulkan.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <set>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

#include "Logger.h"
#include "Swapchain.h"
#include "glfw/Input.h"


class Swapchain;

class StagingUploader {
public:
    explicit StagingUploader(const vma::Allocator &allocator) : allocator(allocator) {
    }

    ~StagingUploader() {
        releaseAll();
    }

    vk::Buffer stage(const void *data, size_t size);

    template<std::ranges::contiguous_range R>
    vk::Buffer stage(R &&data) {
        using T = std::ranges::range_value_t<R>;
        size_t size = data.size() * sizeof(T);
        return stage(data.data(), size);
    }

    template<std::ranges::contiguous_range R>
    void upload(vk::CommandBuffer cmd_buf, R &&data, vk::Buffer dst) {
        using T = std::ranges::range_value_t<R>;
        vk::Buffer staged = stage(std::forward<R>(data));
        cmd_buf.copyBuffer(staged, dst, vk::BufferCopy{.size = data.size() * sizeof(T)});
    }

    void releaseAll();

private:
    const vma::Allocator &allocator;
    std::vector<std::pair<vk::Buffer, vma::Allocation> > active;
};

class InstanceContext {
public:
    glfw::Context glfw = {};
    vk::UniqueInstance instance = {};
    vk::UniqueDebugUtilsMessengerEXT debugMessenger = {};

    std::set<std::string> supportedExtensions;

    InstanceContext();

    [[nodiscard]] vk::Instance get() const {
        return *instance;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanErrorCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                              VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                              void *pUserData);

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

    [[nodiscard]] vk::Device get() const {
        return *device;
    }

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

    [[nodiscard]] glfw::Window get() const {
        return *window;
    }

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
