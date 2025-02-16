#include "Swapchain.h"

#include <GLFW/glfw3.h>

#include "Logger.h"


void Swapchain::create() {
    const auto device = ctx.device.get();
    const auto physicalDevice = ctx.device.physicalDevice;
    const auto surface = *ctx.surface;

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

    auto present_mode_preference = [](const vk::PresentModeKHR mode) {
        if (mode == vk::PresentModeKHR::eMailbox) return 3;
        if (mode == vk::PresentModeKHR::eFifoRelaxed) return 2;
        if (mode == vk::PresentModeKHR::eFifo) return 1;
        if (mode == vk::PresentModeKHR::eImmediate) return 0;
        return -1;
    };
    std::ranges::sort(surface_present_modes, [&present_mode_preference](const auto a, const auto b) {
        return present_mode_preference(a) > present_mode_preference(b);
    });
    presentMode_ = surface_present_modes.front();

    if (present_mode_preference(presentMode_) < 0)
        Logger::panic("No suitable present mode found");
    Logger::info("Using present mode: " + vk::to_string(presentMode_));

    auto surface_present_mode = surface_present_modes[0];
    uint32_t swapchain_image_count = 0;
    if (surface_capabilities.maxImageCount > 0)
        swapchain_image_count = std::min(swapchain_image_count, surface_capabilities.maxImageCount);
    swapchain_image_count = std::max(swapchain_image_count, surface_capabilities.minImageCount);
    imageCount_ = static_cast<int>(swapchain_image_count);
    minImageCount_ = static_cast<int>(surface_capabilities.minImageCount);
    maxImageCount_ = static_cast<int>(std::max(surface_capabilities.maxImageCount, swapchain_image_count));

    surfaceExtents = ctx.window.get().getFramebufferSize();
    surfaceExtents.width = std::clamp(surfaceExtents.width, surface_capabilities.minImageExtent.width,
                                      surface_capabilities.maxImageExtent.width);
    surfaceExtents.height = std::clamp(surfaceExtents.height, surface_capabilities.minImageExtent.height,
                                       surface_capabilities.maxImageExtent.height);

    // need to be destroyed before swapchain is
    swapchainImageViewsSrgb.clear();
    swapchainImageViewsUnorm.clear();

    // allow ceation of a unorm image view
    bool mutable_swapchain_format_supported = ctx.device.supportedExtensions.contains(vk::KHRSwapchainMutableFormatExtensionName);
    surfaceFormatLinear = vk::Format::eUndefined;
    vk::SwapchainCreateFlagsKHR create_falgs = {};
    if (mutable_swapchain_format_supported) {
        if (surfaceFormat.format == vk::Format::eR8G8B8A8Srgb) surfaceFormatLinear = vk::Format::eR8G8B8A8Unorm;
        else if (surfaceFormat.format == vk::Format::eB8G8R8A8Srgb) surfaceFormatLinear = vk::Format::eB8G8R8A8Unorm;
        create_falgs |= vk::SwapchainCreateFlagBitsKHR::eMutableFormat;
    }

    std::vector swapchain_image_formats = {surfaceFormat.format};
    if (mutable_swapchain_format_supported)
        swapchain_image_formats.push_back(surfaceFormatLinear);

    vk::ImageFormatListCreateInfo swapchain_image_formats_info = {
        .viewFormatCount = static_cast<uint32_t>(swapchain_image_formats.size()),
        .pViewFormats = swapchain_image_formats.data()
    };

    swapchain = device.createSwapchainKHRUnique({
        .pNext = &swapchain_image_formats_info,
        .flags = create_falgs,
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
        swapchainImageViewsSrgb.emplace_back(device.createImageViewUnique(image_view_create_info));
        if (surfaceFormatLinear != vk::Format::eUndefined) {
            image_view_create_info.format = surfaceFormatLinear;
            swapchainImageViewsUnorm.emplace_back(device.createImageViewUnique(image_view_create_info));
        }
    }

    depthImageView.reset();
    std::tie(depthImage_, depthImageAllocation) = ctx.device.allocator->createImageUnique(
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

void Swapchain::recreate() {
    // wait if window is minimized
    int width = 0, height = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow *>(ctx.window), &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(static_cast<GLFWwindow *>(ctx.window), &width, &height);
    }
    ctx.device.get().waitIdle();

    create();
}

bool Swapchain::next(const vk::Semaphore &image_available_semaphore) {
    auto extents = ctx.window.get().getFramebufferSize();
    if (surfaceExtents.width != extents.width || surfaceExtents.height != extents.height) {
        recreate();
        return false;
    }

    try {
        auto image_acquistion_result = ctx.device.get().acquireNextImageKHR(*swapchain, UINT64_MAX, image_available_semaphore, nullptr);
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

void Swapchain::present(const vk::Queue &queue, vk::PresentInfoKHR &present_info) {
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

    if(invalid)
        recreate();
}
