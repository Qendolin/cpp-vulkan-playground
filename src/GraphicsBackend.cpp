#define VMA_IMPLEMENTATION

#include "GraphicsBackend.h"

#include "Logger.h"
#include <algorithm>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static VKAPI_ATTR VkBool32 VKAPI_CALL vuklanErrorCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                          VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                          const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                          void *pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        Logger::error(std::string(pCallbackData->pMessage));
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        Logger::warning(std::string(pCallbackData->pMessage));
    } else {
        Logger::debug(std::string(pCallbackData->pMessage));
    }

    return VK_FALSE;
}

vk::Buffer StagingUploader::stage(const void *data, size_t size) {
    vma::AllocationInfo allocation_result = {};
    active.emplace_back() = allocator.createBuffer(
        {
            .size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
        },
        {
            .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            .usage = vma::MemoryUsage::eAuto,
            .requiredFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        },
        &allocation_result);
    std::memcpy(allocation_result.pMappedData, data, size);

    return active.back().first;
}

void StagingUploader::releaseAll() {
    for (auto &[buffer, alloc]: active) {
        allocator.destroyBuffer(buffer, alloc);
    }
    active.clear();
}

GraphicsBackend::GraphicsBackend() {
    glfw = std::make_unique<glfw::Context>();
    window = std::make_unique<glfw::Window>(glfw::WindowCreateInfo{
        .width = 800,
        .height = 800,
        .title = "Vulkan window",
        .resizable = true,
        .clientApi = glfw::ClientApi::None
    });

    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    vk::ApplicationInfo application_info{
        .pApplicationName = "Vulkan Playground",
        .applicationVersion = VK_MAKE_API_VERSION(0, 2024, 8, 13),
        .pEngineName = "Vulkan Playground",
        .engineVersion = VK_MAKE_API_VERSION(0, 2024, 8, 13),
        .apiVersion = VK_API_VERSION_1_3,
    };

    vk::InstanceCreateInfo instance_create_info{
        .pApplicationInfo = &application_info,
    };

    std::vector required_extensions = glfw->getRequiredInstanceExtensions();
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    instance_create_info.enabledExtensionCount = required_extensions.size();
    instance_create_info.ppEnabledExtensionNames = required_extensions.data();

    std::array enabled_layers = {"VK_LAYER_KHRONOS_validation", "VK_LAYER_KHRONOS_synchronization2"};

    instance_create_info.enabledLayerCount = enabled_layers.size();
    instance_create_info.ppEnabledLayerNames = enabled_layers.data();

    instance = vk::createInstanceUnique(instance_create_info);

    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    vk::DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
        .pfnUserCallback = vuklanErrorCallback,
        .pUserData = nullptr,
    };

    debugMessenger = instance->createDebugUtilsMessengerEXTUnique(debug_utils_messenger_create_info);

    surface = window->createWindowSurfaceKHRUnique(*instance);

    std::array required_device_extensions = {
        vk::KHRSwapchainExtensionName, vk::EXTMemoryBudgetExtensionName, vk::KHRDynamicRenderingExtensionName, vk::EXTShaderObjectExtensionName
    };
    for (auto device: instance->enumeratePhysicalDevices()) {
        auto device_properties = device.getProperties();
        if (device_properties.deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
            continue;

        bool missing_required_extension = false;
        auto device_extensions = device.enumerateDeviceExtensionProperties();
        for (auto required_extension: required_device_extensions) {
            if (std::find_if(device_extensions.begin(), device_extensions.end(),
                             [required_extension](auto &&extension) {
                                 return strcmp(extension.extensionName, required_extension);
                             }) == device_extensions.end()) {
                missing_required_extension = true;
                break;
            }
        }
        if (missing_required_extension)
            continue;

        vk::PhysicalDeviceFeatures features = device.getFeatures();
        if (!features.samplerAnisotropy)
            continue;

        uint32_t queue_index = -1;
        for (auto queue_family_properties: device.getQueueFamilyProperties()) {
            queue_index++;

            if (!(queue_family_properties.queueFlags & vk::QueueFlagBits::eGraphics))
                continue;
            if (!device.getSurfaceSupportKHR(queue_index, *surface))
                continue;

            this->graphicsQueueIndex = queue_index;
            this->phyicalDevice = device;
            break;
        }
        if (this->graphicsQueueIndex != -1)
            break;
    }

    if (this->phyicalDevice == nullptr)
        Logger::panic("No suitable GPU found");

    std::string device_name = this->phyicalDevice.getProperties().deviceName;
    Logger::info("Usig GPU: " + device_name);

    constexpr float queue_priority = 1.0f;
    std::array queue_create_infos = {
        vk::DeviceQueueCreateInfo{
            .queueFamilyIndex = graphicsQueueIndex,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        }
    };

    vk::PhysicalDeviceFeatures device_features = {
        .depthClamp = true,
        .samplerAnisotropy = true,
    };

    vk::StructureChain device_create_info = {
        vk::DeviceCreateInfo{
            .queueCreateInfoCount = queue_create_infos.size(),
            .pQueueCreateInfos = queue_create_infos.data(),
            .enabledExtensionCount = required_device_extensions.size(),
            .ppEnabledExtensionNames = required_device_extensions.data(),
            .pEnabledFeatures = &device_features,
        },
        vk::PhysicalDeviceSynchronization2Features{.synchronization2 = true},
        vk::PhysicalDeviceDynamicRenderingFeaturesKHR{.dynamicRendering = true},
        vk::PhysicalDeviceShaderObjectFeaturesEXT{.shaderObject = true},
    };

    device = vk::SharedDevice(phyicalDevice.createDevice(device_create_info.get<vk::DeviceCreateInfo>()));
    graphicsQueue = device->getQueue(graphicsQueueIndex, 0);

    vma::VulkanFunctions vma_vulkan_functions = {
        .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr
    };
    allocator = vma::createAllocatorUnique({
        .flags = vma::AllocatorCreateFlagBits::eExtMemoryBudget,
        .physicalDevice = phyicalDevice,
        .device = *device,
        .pVulkanFunctions = &vma_vulkan_functions,
        .instance = *instance,
        .vulkanApiVersion = application_info.apiVersion,
    });

    stagingMappedMemorySize = 64000000; // 64 MB
    vma::AllocationInfo staging_info = {};
    auto [staging, staging_mem] = allocator->createBufferUnique(
        {
            .size = stagingMappedMemorySize,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
        },
        {
            .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            .usage = vma::MemoryUsage::eAuto,
            .requiredFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        },
        &staging_info);
    this->stagingBuffer = std::move(staging);
    this->stagingAllocation = std::move(staging_mem);
    this->stagingMappedMemory = staging_info.pMappedData;
}

GraphicsBackend::~GraphicsBackend() = default;

void GraphicsBackend::createRenderPass() {
    depthImageFormat = vk::Format::eD32Sfloat; {
        auto [depth_image, depth_mem] = allocator->createImageUnique(
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
        this->depthImage = std::move(depth_image);
        this->depthImageAllocation = std::move(depth_mem);
    }

    depthImageView = device->createImageViewUnique({
        .image = *depthImage,
        .viewType = vk::ImageViewType::e2D,
        .format = depthImageFormat,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .levelCount = 1,
            .layerCount = 1
        }
    });

    // vk::AttachmentDescription color_attachment_description = {
    //     .format = surfaceFormat.format,
    //     .samples = vk::SampleCountFlagBits::e1,
    //     .loadOp = vk::AttachmentLoadOp::eClear,
    //     .storeOp = vk::AttachmentStoreOp::eStore,
    //     .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
    //     .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
    //     .initialLayout = vk::ImageLayout::eUndefined,
    //     .finalLayout = vk::ImageLayout::ePresentSrcKHR
    // };
    //
    // vk::AttachmentReference color_attachment_reference = {
    //     .attachment = 0,
    //     .layout = vk::ImageLayout::eColorAttachmentOptimal
    // };
    //
    // vk::AttachmentDescription depth_attachment_description = {
    //     .format = vk::Format::eD32Sfloat,
    //     .samples = vk::SampleCountFlagBits::e1,
    //     .loadOp = vk::AttachmentLoadOp::eClear,
    //     .storeOp = vk::AttachmentStoreOp::eDontCare,
    //     .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
    //     .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
    //     .initialLayout = vk::ImageLayout::eUndefined,
    //     .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal
    // };
    //
    // vk::AttachmentReference depth_attachment_reference = {
    //     .attachment = 1,
    //     .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal
    // };
    //
    // vk::SubpassDescription subpass_description = {
    //     .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
    //     .colorAttachmentCount = 1,
    //     .pColorAttachments = &color_attachment_reference,
    //     .pDepthStencilAttachment = &depth_attachment_reference
    // };
    //
    // vk::SubpassDependency subpass_dependency = {
    //     .srcSubpass = vk::SubpassExternal,
    //     .dstSubpass = 0,
    //     .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
    //     .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
    //     .srcAccessMask = {},
    //     .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
    // };
    //
    // vk::RenderPassCreateInfo render_pass_create_info = {
    //     .subpassCount = 1,
    //     .pSubpasses = &subpass_description,
    //     .dependencyCount = 1,
    //     .pDependencies = &subpass_dependency,
    // };
    // auto attachment_descriptions = {color_attachment_description, depth_attachment_description};
    // render_pass_create_info.setAttachments(attachment_descriptions);
    //
    // renderPass = device->createRenderPassUnique(render_pass_create_info);
    //
    // framebuffers.clear();
    // for (auto &swapchain_image_view: swapchainColorImageViews) {
    //     vk::FramebufferCreateInfo framebuffer_create_info = {
    //         .renderPass = *renderPass,
    //         .width = surfaceExtents.width,
    //         .height = surfaceExtents.height,
    //         .layers = 1
    //     };
    //     auto attachments = {*swapchain_image_view, *depthImageView};
    //     framebuffer_create_info.setAttachments(attachments);
    //     framebuffers.emplace_back(device->createFramebufferUnique(framebuffer_create_info));
    // }
}

void GraphicsBackend::createSwapchain() {
    auto surface_capabilities = phyicalDevice.getSurfaceCapabilitiesKHR(*surface);
    auto surface_formats = phyicalDevice.getSurfaceFormatsKHR(*surface);
    auto surface_present_modes = phyicalDevice.getSurfacePresentModesKHR(*surface);;

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

    surfaceExtents = window->getFramebufferSize();
    surfaceExtents.width = std::clamp(surfaceExtents.width, surface_capabilities.minImageExtent.width,
                                      surface_capabilities.maxImageExtent.width);
    surfaceExtents.height = std::clamp(surfaceExtents.height, surface_capabilities.minImageExtent.height,
                                       surface_capabilities.maxImageExtent.height);

    vk::SwapchainCreateInfoKHR swapchain_create_info = {
        .surface = *surface,
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
    };

    swapchain = device->createSwapchainKHRUnique(swapchain_create_info);

    swapchainColorImages = device->getSwapchainImagesKHR(*swapchain);
    swapchainColorImageFormat = surfaceFormat.format;

    swapchainColorImageViews.clear();
    for (auto swapchain_image: swapchainColorImages) {
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
        swapchainColorImageViews.emplace_back(std::move(device->createImageViewUnique(image_view_create_info)));
    }
}

void GraphicsBackend::recreateSwapchain() {
    // wait if window is minimized
    int width = 0, height = 0;
    glfwGetFramebufferSize(*window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(*window, &width, &height);
        glfwWaitEvents();
    }

    device->waitIdle();

    // framebuffers.clear();
    swapchainColorImageViews.clear();
    swapchain.reset();

    createSwapchain();
    createRenderPass();
}

void GraphicsBackend::createCommandBuffers(int max_frames_in_flight) {
    vk::CommandPoolCreateInfo command_pool_create_info = {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueIndex
    };
    commandPool = device->createCommandPoolUnique(command_pool_create_info);

    vk::CommandBufferAllocateInfo command_buffer_allocate_info = {
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = static_cast<uint32_t>(max_frames_in_flight)
    };
    commandBuffers = device->allocateCommandBuffersUnique(command_buffer_allocate_info);
}

TransientCommandBuffer GraphicsBackend::createTransientCommandBuffer() const {
    vk::UniqueCommandBuffer cmd_buf = std::move(device->allocateCommandBuffersUnique({
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    }).front());

    cmd_buf->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    return cmd_buf;
}

void GraphicsBackend::submit(TransientCommandBuffer &cmd_buf, bool wait) const {
    if (!wait) {
        cmd_buf->end();
        graphicsQueue.submit(vk::SubmitInfo{
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &*cmd_buf
                             },
                             nullptr);
        cmd_buf.release();
        return;
    }

    vk::UniqueFence fence = device->createFenceUnique({});

    cmd_buf->end();
    graphicsQueue.submit(vk::SubmitInfo{
                             .commandBufferCount = 1,
                             .pCommandBuffers = &*cmd_buf
                         },
                         *fence);
    while (device->waitForFences(*fence, true, UINT64_MAX) == vk::Result::eTimeout) {
    }
    device->resetFences(*fence);
    cmd_buf.release();
}
