#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <functional>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

using TransientCommandBuffer = vk::UniqueHandle<vk::CommandBuffer, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;

class StagingUploader {
public:

    explicit StagingUploader(const vma::Allocator &allocator) : allocator(allocator) {
    }

    ~StagingUploader() {
        releaseAll();
    }

    vk::Buffer stage(const void* data, size_t size);

    template<std::ranges::contiguous_range R>
    vk::Buffer stage(R &&data) {
        using T = std::ranges::range_value_t<R>;
        size_t size = data.size() * sizeof(T);
        return stage(data.data(), size);
    }

    void releaseAll();

private:
    const vma::Allocator &allocator;
    std::vector<std::pair<vk::Buffer, vma::Allocation>> active;
};

class GraphicsBackend {
public:
    std::unique_ptr<glfw::Context> glfw;
    std::unique_ptr<glfw::Window> window;
    vk::UniqueInstance instance;

    vk::UniqueDebugUtilsMessengerEXT debugMessenger;

    vk::UniqueSurfaceKHR surface;
    vk::PhysicalDevice phyicalDevice = nullptr;
    vk::SharedDevice device;
    uint32_t graphicsQueueIndex = -1;
    vk::Queue graphicsQueue = nullptr;

    // vk::UniqueRenderPass renderPass;
    // std::vector<vk::UniqueFramebuffer> framebuffers;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;

    // TODO: should move this up
    vma::UniqueAllocator allocator;

    vma::UniqueBuffer stagingBuffer;

    vk::SurfaceFormatKHR surfaceFormat;
    vk::Extent2D surfaceExtents;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapchainColorImages;
    std::vector<vk::UniqueImageView> swapchainColorImageViews;
    vk::Format swapchainColorImageFormat = vk::Format::eUndefined;

    vma::UniqueImage depthImage;
    vma::UniqueAllocation depthImageAllocation;
    vk::UniqueImageView depthImageView;
    vk::Format depthImageFormat = vk::Format::eUndefined;

private:
    vma::UniqueAllocation stagingAllocation;
    void *stagingMappedMemory;
    vk::DeviceSize stagingMappedMemorySize;

public:
    GraphicsBackend();

    ~GraphicsBackend();

    void createRenderPass();

    void createSwapchain();

    void recreateSwapchain();

    void createCommandBuffers(int max_frames_in_flight);

    [[nodiscard]] TransientCommandBuffer createTransientCommandBuffer() const;

    void submit(TransientCommandBuffer &cmd_buf, bool wait) const;

    template<std::ranges::contiguous_range R>
    void copyToStaging(R &&data) const {
        using T = std::ranges::range_value_t<R>;
        vk::DeviceSize size = data.size() * sizeof(T);
        if (size > stagingMappedMemorySize) {
            throw std::exception("buffer to big for staging");
        }
        std::memcpy(stagingMappedMemory, data.data(), size);
    }

    template<std::ranges::contiguous_range R>
    void uploadWithStaging(R &&data, vk::Buffer &dst) const {
        using T = std::ranges::range_value_t<R>;

        vk::DeviceSize size = data.size() * sizeof(T);

        vk::DeviceSize offset = 0;
        vk::DeviceSize left = size;

        while (left > 0) {
            vk::DeviceSize block_size = std::min(size, stagingMappedMemorySize);
            std::memcpy(stagingMappedMemory, data.data() + offset, block_size);

            TransientCommandBuffer cmd_buf = createTransientCommandBuffer();
            cmd_buf->copyBuffer(*stagingBuffer, dst, vk::BufferCopy{.dstOffset = offset, .size = size});
            submit(cmd_buf, true);

            left -= std::min(left, block_size);
        }
    }
};
