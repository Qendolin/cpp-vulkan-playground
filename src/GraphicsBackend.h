#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <functional>
#include <set>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

#include "Logger.h"

using TransientCommandBuffer = vk::UniqueHandle<vk::CommandBuffer, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;

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

class Swapchain {
    vma::Allocator allocator;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    glfw::Window window;
    vk::SurfaceKHR surface;

    bool mutableSwapchainFormatSupported;
    vk::SurfaceFormatKHR surfaceFormat = {.format = vk::Format::eUndefined, .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::Format surfaceFormatLinear = vk::Format::eUndefined;

    vk::Extent2D surfaceExtents;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapchainImages;
    std::vector<vk::UniqueImageView> swapchainImageViewsSrgb;
    std::vector<vk::UniqueImageView> swapchainImageViewsUnorm;

    vma::UniqueImage depthImage_;
    vma::UniqueAllocation depthImageAllocation;
    vk::UniqueImageView depthImageView;
    const vk::Format depthImageFormat = vk::Format::eD32Sfloat;

    uint32_t activeImageIndex = 0;
    int imageCount_ = 0;
    int minImageCount_ = 0;
    int maxImageCount_ = 0;
    vk::PresentModeKHR presentMode_ = vk::PresentModeKHR::eImmediate;
    bool invalid = true;

public:
    Swapchain(vma::Allocator allocator, vk::PhysicalDevice physical_device, vk::Device device, glfw::Window window, vk::SurfaceKHR surface,
              bool mutable_swapchain_format_supported)
        : allocator(allocator), physicalDevice(physical_device), device(device), window(window), surface(surface),
          mutableSwapchainFormatSupported(mutable_swapchain_format_supported) {
        create();
    }

    [[nodiscard]] vk::Format colorFormatSrgb() const {
        return surfaceFormat.format;
    }

    [[nodiscard]] vk::Format colorFormatLinear() const {
        if (surfaceFormatLinear == vk::Format::eUndefined)
            return colorFormatSrgb();
        return surfaceFormatLinear;
    }

    [[nodiscard]] vk::Format depthFormat() const {
        return depthImageFormat;
    }

    [[nodiscard]] int imageCount() const {
        return imageCount_;
    }

    [[nodiscard]] int minImageCount() const {
        return minImageCount_;
    }

    [[nodiscard]] int maxImageCount() const {
        return maxImageCount_;
    }

    [[nodiscard]] vk::PresentModeKHR presentMode() const {
        return presentMode_;
    }

    [[nodiscard]] vk::Extent2D extents() const {
        return surfaceExtents;
    }

    [[nodiscard]] vk::Rect2D area() const {
        return {{}, surfaceExtents};
    }

    [[nodiscard]] float width() const {
        return static_cast<float>(surfaceExtents.width);
    }

    [[nodiscard]] float height() const {
        return static_cast<float>(surfaceExtents.height);
    }

    [[nodiscard]] vk::Image colorImage() const {
        return swapchainImages.at(activeImageIndex);
    }

    [[nodiscard]] vk::ImageView colorViewSrgb() const {
        return *swapchainImageViewsSrgb.at(activeImageIndex);
    }

    [[nodiscard]] vk::ImageView colorViewLinear() const {
        if (surfaceFormatLinear == vk::Format::eUndefined)
            return colorViewSrgb();
        return *swapchainImageViewsUnorm.at(activeImageIndex);
    }

    [[nodiscard]] vk::Image depthImage() const {
        return *depthImage_;
    }

    [[nodiscard]] vk::ImageView depthView() const {
        return *depthImageView;
    }

    void create();

    void recreate() {
        // wait if window is minimized
        int width = 0, height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow *>(window), &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(static_cast<GLFWwindow *>(window), &width, &height);
        }
        device.waitIdle();

        create();
    }

    void invalidate() {
        invalid = true;
    }

    bool next(const vk::Semaphore &image_available_semaphore) {
        auto extents = window.getFramebufferSize();
        if (surfaceExtents.width != extents.width || surfaceExtents.height != extents.height) {
            recreate();
            return false;
        }

        try {
            auto image_acquistion_result = device.acquireNextImageKHR(*swapchain, UINT64_MAX, image_available_semaphore, nullptr);
            if (image_acquistion_result.result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
                invalidate();
            }
            activeImageIndex = image_acquistion_result.value;
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            invalidate();
        }

        if (invalid) {
            recreate();
            return false;
        }
        return true;
    }

    void present(const vk::Queue &queue, vk::PresentInfoKHR &present_info) {
        present_info
                .setSwapchains(*swapchain)
                .setImageIndices(activeImageIndex);

        try {
            vk::Result result = queue.presentKHR(present_info);
            if (result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
                invalidate();
            }
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            invalidate();
        }

        if (invalid) {
            recreate();
        }
    }
};

class GraphicsBackend {
public:
    glfw::Context glfw;
    glfw::UniqueWindow window;
    vk::UniqueInstance instance;

    vk::UniqueDebugUtilsMessengerEXT debugMessenger;

    vk::UniqueSurfaceKHR surface;
    vk::PhysicalDevice phyicalDevice = nullptr;
    vk::SharedDevice device;
    uint32_t graphicsQueueIndex = -1;
    vk::Queue graphicsQueue = nullptr;
    std::set<std::string> supportedDeviceExtensions;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;

    vma::UniqueAllocator allocator;

public:
    GraphicsBackend();

    ~GraphicsBackend();

    void createCommandBuffers(int max_frames_in_flight);

    [[nodiscard]] TransientCommandBuffer createTransientCommandBuffer() const;

    void submit(TransientCommandBuffer &cmd_buf, bool wait) const;

    bool supportsExtension(const char *name) const {
        return supportedDeviceExtensions.contains(std::string(name));
    }
};
