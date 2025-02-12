#define VMA_IMPLEMENTATION

#include "GraphicsBackend.h"

#include "Logger.h"
#include <algorithm>
#include <set>
#include <ranges>

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

void Swapchain::create() {
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
    uint32_t swapchain_image_count = 2;
    if (surface_capabilities.maxImageCount > 0)
        swapchain_image_count = std::min(swapchain_image_count, surface_capabilities.maxImageCount);
    swapchain_image_count = std::max(swapchain_image_count, surface_capabilities.minImageCount);
    imageCount_ = static_cast<int>(swapchain_image_count);
    minImageCount_ = static_cast<int>(surface_capabilities.minImageCount);
    maxImageCount_ = static_cast<int>(std::max(surface_capabilities.maxImageCount, swapchain_image_count));

    surfaceExtents = window.getFramebufferSize();
    surfaceExtents.width = std::clamp(surfaceExtents.width, surface_capabilities.minImageExtent.width,
                                      surface_capabilities.maxImageExtent.width);
    surfaceExtents.height = std::clamp(surfaceExtents.height, surface_capabilities.minImageExtent.height,
                                       surface_capabilities.maxImageExtent.height);

    // need to be destroyed before swapchain is
    swapchainImageViewsSrgb.clear();
    swapchainImageViewsUnorm.clear();

    // allow ceation of a unorm image view
    surfaceFormatLinear = vk::Format::eUndefined;
    vk::SwapchainCreateFlagsKHR create_falgs = {};
    if (mutableSwapchainFormatSupported) {
        if (surfaceFormat.format == vk::Format::eR8G8B8A8Srgb) surfaceFormatLinear = vk::Format::eR8G8B8A8Unorm;
        else if (surfaceFormat.format == vk::Format::eB8G8R8A8Srgb) surfaceFormatLinear = vk::Format::eB8G8R8A8Unorm;
        create_falgs |= vk::SwapchainCreateFlagBitsKHR::eMutableFormat;
    }

    std::vector swapchain_image_formats = {surfaceFormat.format};
    if (mutableSwapchainFormatSupported)
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

GraphicsBackend::GraphicsBackend() {
    window = glfw::UniqueWindow(glfw::WindowCreateInfo{
        .width = 1600,
        .height = 900,
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

    std::vector required_extensions = glfw::Context::getRequiredInstanceExtensions();
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    instance_create_info.enabledExtensionCount = required_extensions.size();
    instance_create_info.ppEnabledExtensionNames = required_extensions.data();

    std::array enabled_layers = {"VK_LAYER_KHRONOS_validation"};

    instance_create_info.enabledLayerCount = enabled_layers.size();
    instance_create_info.ppEnabledLayerNames = enabled_layers.data();

    Logger::info("Available Layers:");
    for (auto layer_property: vk::enumerateInstanceLayerProperties()) {
        Logger::info(std::format("- {}: {}", std::string(layer_property.layerName.data()), std::string(layer_property.description.data())));
    }

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
    std::array optional_device_extensions = {vk::KHRSwapchainMutableFormatExtensionName};

    int best_device_score = -1;
    for (auto device: instance->enumeratePhysicalDevices()) {
        int score = 0;
        auto device_properties = device.getProperties();

        bool missing_required_extension = false;
        auto device_extensions = device.enumerateDeviceExtensionProperties();
        auto extension_names = std::views::transform(device_extensions, [](const auto &e) { return std::string(e.extensionName.data()); });
        supportedDeviceExtensions = std::set(extension_names.begin(), extension_names.end());
        for (auto ext: required_device_extensions) {
            if (!supportedDeviceExtensions.contains(std::string(ext))) {
                missing_required_extension = true;
                break;
            }
        }
        if (missing_required_extension)
            continue;

        if (device_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            score += 1000;

        vk::PhysicalDeviceFeatures features = device.getFeatures();
        if (features.samplerAnisotropy)
            score += 50;

        if (score <= best_device_score)
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
            best_device_score = score;
        }
    }

    if (!phyicalDevice)
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

    auto device_extensions = std::vector<const char *>();
    device_extensions.insert(device_extensions.end(), required_device_extensions.begin(), required_device_extensions.end());
    for (auto ext: optional_device_extensions) {
        if (supportedDeviceExtensions.contains(std::string(ext))) {
            device_extensions.push_back(ext);
        }
    }

    vk::StructureChain device_create_info = {
        vk::DeviceCreateInfo{
            .queueCreateInfoCount = queue_create_infos.size(),
            .pQueueCreateInfos = queue_create_infos.data(),
            .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
            .ppEnabledExtensionNames = device_extensions.data(),
            .pEnabledFeatures = &device_features,
        },
        vk::PhysicalDeviceSynchronization2Features{.synchronization2 = true},
        vk::PhysicalDeviceDynamicRenderingFeaturesKHR{.dynamicRendering = true},
        vk::PhysicalDeviceShaderObjectFeaturesEXT{.shaderObject = true},
        vk::PhysicalDeviceInlineUniformBlockFeatures{.inlineUniformBlock = true},
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
}

GraphicsBackend::~GraphicsBackend() = default;

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
