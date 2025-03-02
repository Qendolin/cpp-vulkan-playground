#pragma once

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

namespace vk {
    class PhysicalDevice;
    class Queue;
    class Device;
}

class TracyContext {
public:
    static inline thread_local TracyVkCtx Vulkan = nullptr;

    static void create(
            const vk::PhysicalDevice &physical_device, const vk::Device &device, const vk::Queue &queue, uint32_t queue_family
    );

    static void destroy(const vk::Device &device);
};
