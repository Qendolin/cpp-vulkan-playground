#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
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

    GraphicsBackend();

    ~GraphicsBackend();

    void createRenderPass();

    void createSwapchain();

    void recreateSwapchain();

    void createCommandBuffers(int max_frames_in_flight);
};
