#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <functional>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

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

    vk::SurfaceFormatKHR surfaceFormat;
    vk::Extent2D surfaceExtents;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::UniqueImageView> swapchainColorImages;

    vk::UniqueRenderPass renderPass;
    std::vector<vk::UniqueFramebuffer> framebuffers;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;

    vma::UniqueAllocator allocator;

    vma::UniqueBuffer stagingBuffer;

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

    void submitImmediate(std::function<void(vk::CommandBuffer cmd_buf)> &&func);

    template<std::ranges::contiguous_range R>
    void copyToStaging(R &&data) {
        using T = std::ranges::range_value_t<R>;
        vk::DeviceSize size = data.size() * sizeof(T);
        if (size > stagingMappedMemorySize) {
            throw std::exception("buffer to big for staging");
        }
        std::memcpy(stagingMappedMemory, data.data(), size);
    }

    template<std::ranges::contiguous_range R>
    void uploadWithStaging(R &&data, vk::Buffer &dst) {
        using T = std::ranges::range_value_t<R>;

        vk::DeviceSize size = data.size() * sizeof(T);

        vk::DeviceSize offset = 0;
        vk::DeviceSize left = size;

        while (left > 0) {
            vk::DeviceSize block_size = std::min(size, stagingMappedMemorySize);
            std::memcpy(stagingMappedMemory, data.data() + offset, block_size);

            submitImmediate([this, dst, size, offset](const vk::CommandBuffer &cmd_buf) {
                cmd_buf.copyBuffer(*stagingBuffer, dst, vk::BufferCopy{.dstOffset = offset, .size = size});
            });

            left -= std::min(left, block_size);
        }
    }
};
