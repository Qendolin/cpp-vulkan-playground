#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

#include "glfw/Context.h"
#include "glfw/Window.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <functional>
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

    void releaseAll();

private:
    const vma::Allocator &allocator;
    std::vector<std::pair<vk::Buffer, vma::Allocation> > active;
};

class Swapchain {
    vma::Allocator allocator;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    glfw::Window &window;
    vk::SurfaceKHR surface;

    vk::SurfaceFormatKHR surfaceFormat = {.format = vk::Format::eUndefined, .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::Extent2D surfaceExtents;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapchainImages;
    std::vector<vk::UniqueImageView> swapchainImageViews;

    vma::UniqueImage depthImage_;
    vma::UniqueAllocation depthImageAllocation;
    vk::UniqueImageView depthImageView;
    const vk::Format depthImageFormat = vk::Format::eD32Sfloat;

    uint32_t activeImageIndex = 0;
    bool invalid = true;

public:
    Swapchain(vma::Allocator allocator, vk::PhysicalDevice physical_device, vk::Device device, glfw::Window &window, vk::SurfaceKHR surface)
        : allocator(allocator), physicalDevice(physical_device), device(device), window(window), surface(surface) {
        create();
    }

    [[nodiscard]] vk::Format colorFormat() const {
        return surfaceFormat.format;
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

    [[nodiscard]] vk::ImageView colorView() const {
        return *swapchainImageViews.at(activeImageIndex);
    }

    [[nodiscard]] vk::Image depthImage() const {
        return *depthImage_;
    }

    [[nodiscard]] vk::ImageView depthView() const {
        return *depthImageView;
    }

    void create() {
        auto surface_capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        auto surface_formats = physicalDevice.getSurfaceFormatsKHR(surface);
        auto surface_present_modes = physicalDevice.getSurfacePresentModesKHR(surface);;

        auto surface_format_iter = std::find_if(surface_formats.begin(), surface_formats.end(), [](auto &&format) {
            return (format.format == vk::Format::eB8G8R8A8Srgb || format.format == vk::Format::eR8G8B8A8Srgb) && format.colorSpace ==
                   vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        if (surface_format_iter == surface_formats.end())
            Logger::panic("No suitable surface fromat found");
        surfaceFormat = surface_format_iter[0];

        auto surface_fifo_present_mode_iter = std::find_if(surface_present_modes.begin(), surface_present_modes.end(),
                                                           [](auto &&mode) {
                                                               return mode == vk::PresentModeKHR::eFifo;
                                                           });

        if (surface_fifo_present_mode_iter == surface_present_modes.end())
            Logger::panic("No suitable present mode found");

        auto surface_present_mode = surface_present_modes[0];
        auto swapchain_image_count = surface_capabilities.maxImageCount + 1;
        if (surface_capabilities.maxImageCount > 0 && swapchain_image_count > surface_capabilities.maxImageCount)
            swapchain_image_count = surface_capabilities.maxImageCount;

        surfaceExtents = window.getFramebufferSize();
        surfaceExtents.width = std::clamp(surfaceExtents.width, surface_capabilities.minImageExtent.width,
                                          surface_capabilities.maxImageExtent.width);
        surfaceExtents.height = std::clamp(surfaceExtents.height, surface_capabilities.minImageExtent.height,
                                           surface_capabilities.maxImageExtent.height);

        // need to be destroyed before swapchain is
        swapchainImageViews.clear();
        swapchain = device.createSwapchainKHRUnique({
            .surface = surface,
            .minImageCount = swapchain_image_count,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = surfaceExtents,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surface_capabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = surface_present_mode,
            .clipped = true,
            .oldSwapchain = *swapchain,
        });

        swapchainImages = device.getSwapchainImagesKHR(*swapchain);

        swapchainImageViews.clear();
        for (const auto &swapchain_image: swapchainImages) {
            vk::ImageViewCreateInfo image_view_create_info = {
                .image = swapchain_image,
                .viewType = vk::ImageViewType::e2D,
                .format = surfaceFormat.format,
                .components = vk::ComponentMapping{},
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            swapchainImageViews.emplace_back(device.createImageViewUnique(image_view_create_info));
        }

        depthImageView.reset();
        std::tie(depthImage_, depthImageAllocation) = allocator.createImageUnique(
            {
                .imageType = vk::ImageType::e2D,
                .format = depthImageFormat,
                .extent = {
                    .width = surfaceExtents.width,
                    .height = surfaceExtents.height,
                    .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            },
            {
                .usage = vma::MemoryUsage::eAutoPreferDevice,
                .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
            });

        depthImageView = device.createImageViewUnique({
            .image = *depthImage_,
            .viewType = vk::ImageViewType::e2D,
            .format = depthImageFormat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 1
            }
        });

        invalid = false;
    }

    void recreate() {
        // wait if window is minimized
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &width, &height);
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
    std::unique_ptr<glfw::Context> glfw;
    std::unique_ptr<glfw::Window> window;
    vk::UniqueInstance instance;

    vk::UniqueDebugUtilsMessengerEXT debugMessenger;

    vk::UniqueSurfaceKHR surface;
    vk::PhysicalDevice phyicalDevice = nullptr;
    vk::SharedDevice device;
    uint32_t graphicsQueueIndex = -1;
    vk::Queue graphicsQueue = nullptr;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;

    // TODO: should move this up
    vma::UniqueAllocator allocator;

    vma::UniqueBuffer stagingBuffer;

private:
    vma::UniqueAllocation stagingAllocation;
    void *stagingMappedMemory;
    vk::DeviceSize stagingMappedMemorySize;

public:
    GraphicsBackend();

    ~GraphicsBackend();

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
