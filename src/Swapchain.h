#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

#include "GraphicsBackend.h"

class WindowContext;

class Swapchain {
    const WindowContext& ctx;

    vk::SurfaceFormatKHR surfaceFormat = {.format = vk::Format::eUndefined, .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::Format surfaceFormatLinear = vk::Format::eUndefined;

    vk::Extent2D surfaceExtents = {};
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
    explicit Swapchain(const WindowContext& ctx) : ctx(ctx) {
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

    void recreate();

    void invalidate() {
        invalid = true;
    }

    bool next(const vk::Semaphore &image_available_semaphore);

    void present(const vk::Queue &queue, vk::PresentInfoKHR &present_info);
};
