// vulkan first
#include <vulkan/vulkan.hpp>
// then tracy
#include "Tracy.h"

// then rest
#include "../Logger.h"


struct VulkanData {
    vk::CommandPool commandPool;
    vk::CommandBuffer commandBuffer;
};

static inline thread_local VulkanData *VulkanData_ = nullptr;


void TracyContext::create(
        const vk::PhysicalDevice &physical_device, const vk::Device &device, const vk::Queue &queue, uint32_t queue_family
) {
#ifdef TRACY_ENABLE
    if (Vulkan) {
        Logger::panic("Tracy context already created");
    }
    VulkanData_ = new VulkanData();
    VulkanData_->commandPool = device.createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = queue_family,
    });
    VulkanData_->commandBuffer =
            device.allocateCommandBuffers({.commandPool = VulkanData_->commandPool, .commandBufferCount = 1}).front();
    Vulkan = TracyVkContext(physical_device, device, queue, VulkanData_->commandBuffer);
#endif
}

void TracyContext::destroy(const vk::Device &device) {
#ifdef TRACY_ENABLE
    if (Vulkan) {
        Logger::panic("Tracy context already destroyed");
    }

    TracyVkDestroy(Vulkan);
    Vulkan = nullptr;

    device.freeCommandBuffers(VulkanData_->commandPool, {VulkanData_->commandBuffer});
    device.destroyCommandPool(VulkanData_->commandPool);
    delete VulkanData_;
#endif
}
