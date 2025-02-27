#define VMA_IMPLEMENTATION

#include "GraphicsBackend.h"

#include <algorithm>
#include <format>
#include <cstring>
#include <set>
#include <ranges>
#include <vulkan/vulkan.h>
#include <glfw/glfw3.h>

#include <vulkan/vulkan.hpp>

#include "Logger.h"


VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static VkBool32 vulkanErrorCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void * /*pUserData*/) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        Logger::error(std::string(pCallbackData->pMessage));
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        Logger::warning(std::string(pCallbackData->pMessage));
    } else {
        Logger::debug(std::string(pCallbackData->pMessage));
    }

    return VK_FALSE;
}

InstanceContext::InstanceContext() {
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

    std::vector<const char *> layers;
#ifndef NDEBUG
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
    instance_create_info.setPEnabledLayerNames(layers);

    std::vector extensions = glfw::Context::getRequiredInstanceExtensions();
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
    extensions.push_back(vk::KHRGetSurfaceCapabilities2ExtensionName);
    instance_create_info.setPEnabledExtensionNames(extensions);

    Logger::info("Available Layers:");
    for (auto layer_property: vk::enumerateInstanceLayerProperties()) {
        Logger::info(std::format("- {}: {}", std::string(layer_property.layerName.data()), std::string(layer_property.description.data())));
    }

    auto extension_properties = vk::enumerateInstanceExtensionProperties();
    auto extension_names = std::views::transform(extension_properties, [](const auto &e) { return std::string(e.extensionName.data()); });
    supportedExtensions = std::set(extension_names.begin(), extension_names.end());

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
        .pfnUserCallback = &vulkanErrorCallback,
        .pUserData = nullptr,
    };

    debugMessenger = instance->createDebugUtilsMessengerEXTUnique(debug_utils_messenger_create_info);
}

class DeviceSelector {
    vk::Instance instance;
    std::vector<vk::PhysicalDevice> physicalDevices;
    std::vector<std::string> requriedExtensions;

    static bool hasExtensions(vk::PhysicalDevice device, std::span<std::string> names) {
        auto extension_properties = device.enumerateDeviceExtensionProperties();
        auto extension_names = std::views::transform(extension_properties, [](const auto &e) { return std::string(e.extensionName.data()); });
        auto extension_set = std::set(extension_names.begin(), extension_names.end());
        return std::ranges::all_of(names,
                                   [&](const auto &name) { return extension_set.contains(name); });
    }

    static bool hasQueues(vk::PhysicalDevice device, vk::QueueFlags queues) {
        vk::QueueFlags available = {};
        for (auto queue: device.getQueueFamilyProperties())
            available |= queue.queueFlags;
        return (available & queues) == queues;
    }

    static bool hasPresentationSupport(vk::Instance instance, vk::PhysicalDevice device) {
        return std::ranges::any_of(std::views::enumerate(device.getQueueFamilyProperties()),
                                   [&](const auto &entry) {
                                       const auto &[index, queue] = entry;
                                       return (queue.queueFlags & vk::QueueFlagBits::eGraphics)
                                              && glfwGetPhysicalDevicePresentationSupport(instance, device, static_cast<uint32_t>(index)) == GLFW_TRUE;
                                   });
    }

    bool isAcceptable(vk::PhysicalDevice device) {
        return hasExtensions(device, requriedExtensions)
               && hasQueues(device, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)
               && hasPresentationSupport(instance, device);
    }

    float static score(vk::PhysicalDevice device) {
        float score = 0.0;
        auto properties = device.getProperties();
        auto features = device.getFeatures();

        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            score += 10000;

        if (features.samplerAnisotropy)
            score += 50;

        if (features.depthClamp)
            score += 50;

        return score;
    }

public:
    explicit DeviceSelector(vk::Instance instance, std::vector<vk::PhysicalDevice> &&devices) : instance(instance), physicalDevices(std::move(devices)) {
    }

    void setRequiredExtensions(std::span<const char *> names) {
        requriedExtensions.clear();
        std::ranges::transform(names.cbegin(), names.cend(), std::back_inserter(requriedExtensions),
                               [](auto name) { return std::string(name); });
    }

    std::optional<vk::PhysicalDevice> select() {
        auto acceptable = std::views::filter(physicalDevices,
                                             [this](auto device) { return isAcceptable(device); });
        if (acceptable.empty()) {
            return std::nullopt;
        }

        return std::ranges::max(acceptable, {}, score);
    }
};

DeviceContext::DeviceContext() {
    const auto &ctx = instace;
    std::array required_extensions = {
        vk::KHRSwapchainExtensionName, vk::EXTMemoryBudgetExtensionName, vk::KHRDynamicRenderingExtensionName, vk::EXTShaderObjectExtensionName,
        vk::KHRUniformBufferStandardLayoutExtensionName, vk::EXTScalarBlockLayoutExtensionName
    };
    std::array optional_extensions = {vk::KHRSwapchainMutableFormatExtensionName};

    DeviceSelector selector(*ctx.instance, ctx.instance->enumeratePhysicalDevices());
    selector.setRequiredExtensions(required_extensions);
    auto physical_device_opt = selector.select();
    if (!physical_device_opt.has_value())
        Logger::panic("No suitable GPU found");

    physicalDevice = physical_device_opt.value();
    Logger::info(std::format("Usig GPU: {}", std::string_view(physicalDevice.getProperties().deviceName)));

    const auto find_queue = [](std::span<vk::QueueFamilyProperties> queues, vk::QueueFlags required, vk::QueueFlags excluded) -> std::optional<uint32_t> {
        const auto view = std::views::enumerate(queues);
        const auto it = std::ranges::find_if(view,
                                             [required, excluded](const auto &entry) {
                                                 const auto &[index, queue] = entry;
                                                 if (queue.queueFlags & excluded) return false;
                                                 if (queue.queueFlags & required) return true;
                                                 return false;
                                             });
        return it == view.end() ? std::nullopt : std::optional(static_cast<int32_t>(std::get<0>(*it)));
    };
    auto queue_families = physicalDevice.getQueueFamilyProperties();
    // A graphics + compute queue always supports transfer
    mainQueueFamily = find_queue(queue_families, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, {})
            .value_or(-1u);
    // Look for a queue that has only compute, no graphics
    computeQueueFamily = find_queue(queue_families, vk::QueueFlagBits::eCompute,
                                    vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eVideoDecodeKHR | vk::QueueFlagBits::eVideoEncodeKHR)
            .value_or(mainQueueFamily);
    // Look for a queue that has only transfer, no compute or graphics
    transferQueueFamily = find_queue(queue_families, vk::QueueFlagBits::eTransfer,
                                     vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eVideoDecodeKHR |
                                     vk::QueueFlagBits::eVideoEncodeKHR)
            .value_or(mainQueueFamily);

    std::vector<std::vector<float> > queue_priorities(queue_families.size());
    queue_priorities[mainQueueFamily].push_back(1.0);
    queue_priorities[computeQueueFamily].push_back(1.0);
    queue_priorities[transferQueueFamily].push_back(1.0);

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos =
            std::views::iota(0u, static_cast<uint32_t>(queue_families.size()))
            | std::views::transform([&queue_priorities](auto index) {
                return vk::DeviceQueueCreateInfo{.queueFamilyIndex = index, .pQueuePriorities = queue_priorities[index].data()};
            })
            | std::ranges::to<std::vector>();

    uint32_t main_queue_result_index = queue_create_infos[mainQueueFamily].queueCount++;
    uint32_t compute_queue_result_index = queue_create_infos[computeQueueFamily].queueCount++;
    uint32_t transfer_queue_result_index = queue_create_infos[transferQueueFamily].queueCount++;

    // fixup invlaid create infos
    std::erase_if(queue_create_infos, [](const auto &info) { return info.queueCount == 0; });
    for (auto &queue_create_info: queue_create_infos) {
        auto max = queue_families.at(queue_create_info.queueFamilyIndex).queueCount;
        if (queue_create_info.queueCount > max) {
            queue_create_info.queueCount = max;
        }
    }
    main_queue_result_index = std::min(main_queue_result_index, queue_create_infos[mainQueueFamily].queueCount - 1);
    compute_queue_result_index = std::min(compute_queue_result_index, queue_create_infos[computeQueueFamily].queueCount - 1);
    transfer_queue_result_index = std::min(transfer_queue_result_index, queue_create_infos[transferQueueFamily].queueCount - 1);

    auto extension_properties = physicalDevice.enumerateDeviceExtensionProperties();
    auto extension_names = std::views::transform(extension_properties, [](const auto &e) { return std::string(e.extensionName.data()); });
    supportedExtensions = std::set(extension_names.begin(), extension_names.end());

    auto enabled_extensions = std::vector<const char *>();
    enabled_extensions.insert(enabled_extensions.end(), required_extensions.begin(), required_extensions.end());
    for (auto ext: optional_extensions) {
        if (supportedExtensions.contains(std::string(ext))) {
            enabled_extensions.push_back(ext);
        }
    }

    vk::PhysicalDeviceFeatures enabled_features = {
        .depthClamp = true,
        .samplerAnisotropy = true,
    };
    vk::StructureChain device_create_info = {
        vk::DeviceCreateInfo{
            .pEnabledFeatures = &enabled_features,
        }
        .setQueueCreateInfos(queue_create_infos)
        .setPEnabledExtensionNames(enabled_extensions),
        vk::PhysicalDeviceSynchronization2Features{.synchronization2 = true},
        vk::PhysicalDeviceDynamicRenderingFeaturesKHR{.dynamicRendering = true},
        vk::PhysicalDeviceShaderObjectFeaturesEXT{.shaderObject = true},
        vk::PhysicalDeviceInlineUniformBlockFeatures{.inlineUniformBlock = true},
    };

    device = physicalDevice.createDeviceUnique(device_create_info.get<vk::DeviceCreateInfo>());
    mainQueue = device->getQueue(mainQueueFamily, main_queue_result_index);
    computeQueue = device->getQueue(computeQueueFamily, compute_queue_result_index);
    transferQueue = device->getQueue(transferQueueFamily, transfer_queue_result_index);

    vma::VulkanFunctions vma_vulkan_functions = {
        .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr
    };
    allocator = vma::createAllocatorUnique({
        .flags = vma::AllocatorCreateFlagBits::eExtMemoryBudget,
        .physicalDevice = physicalDevice,
        .device = *device,
        .pVulkanFunctions = &vma_vulkan_functions,
        .instance = *ctx.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    });
}

WindowContext::WindowContext(const Config &config): instance(device.instace) {
    const auto &ctx = device;
    window = glfw::UniqueWindow({
        .width = config.width,
        .height = config.height,
        .title = config.title,
        .resizable = true,
    });
    surface = window->createWindowSurfaceKHRUnique(ctx.instace.get());
    window->centerOnScreen();

    if (!ctx.physicalDevice.getSurfaceSupportKHR(ctx.mainQueueFamily, *surface)) {
        // I think checking glfwGetPhysicalDevicePresentationSupport should ensure that this cannot happen
        Logger::panic("Selected queue does not allow presentation on given surface");
    }

    input = std::make_unique<glfw::Input>(*window);
}

AppContext::AppContext(const WindowContext::Config &window_config): window(window_config), device(window.device), instance(window.device.instace) {
    swapchain = std::make_unique<Swapchain>(window);
}
