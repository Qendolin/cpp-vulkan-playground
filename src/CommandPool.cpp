#include "CommandPool.h"

CommandPool::CommandPool(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode): device(device), queue(queue), mode(mode) {
    vk::CommandPoolCreateFlags flags = {};
    if (mode == UseMode::Single || mode == UseMode::Reset)
        flags |= vk::CommandPoolCreateFlagBits::eTransient;
    pool = device.createCommandPoolUnique({
        .flags = flags,
        .queueFamilyIndex = queue_index
    });

    fence = device.createFenceUnique({});
}

vk::CommandBuffer CommandPool::create() {
    auto buffer = device.allocateCommandBuffers({
        .commandPool = *pool,
        .commandBufferCount = 1,
    }).front();
    if (mode == UseMode::Single)
        buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    return buffer;
}

void CommandPool::reset() {
    vk::CommandPoolResetFlags flags = {};
    if (mode == UseMode::Single)
        flags |= vk::CommandPoolResetFlagBits::eReleaseResources;
    device.resetCommandPool(*pool, flags);
}

void CommandPool::freeAll() {
    device.resetCommandPool(*pool, vk::CommandPoolResetFlagBits::eReleaseResources);
}

void CommandPool::submit(vk::CommandBuffer buffer) {
    if (mode == UseMode::Single)
        buffer.end();

    queue.submit(vk::SubmitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &buffer
    });
}

void CommandPool::submitAndWait(vk::CommandBuffer buffer) {
    if (mode == UseMode::Single)
        buffer.end();

    queue.submit(vk::SubmitInfo{
                     .commandBufferCount = 1,
                     .pCommandBuffers = &buffer
                 },
                 *fence);

    while (device.waitForFences(*fence, true, UINT64_MAX) == vk::Result::eTimeout) {
    }
    device.resetFences(*fence);
}
