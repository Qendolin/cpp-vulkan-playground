#include "CommandPool.h"

#include <utility>

#include "Logger.h"

CommandPool::CommandPool(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode)
    : device_(device), queue_(queue), mode_(mode) {
    vk::CommandPoolCreateFlags flags = {};
    if (mode == UseMode::Single || mode == UseMode::Reset)
        flags |= vk::CommandPoolCreateFlagBits::eTransient;
    pool = device.createCommandPoolUnique({.flags = flags, .queueFamilyIndex = queue_index});

    fence_ = device.createFenceUnique({});
}

vk::CommandBuffer CommandPool::create() const {
    auto buffer = device_.allocateCommandBuffers({.commandPool = *pool, .commandBufferCount = 1}).front();
    if (mode_ == UseMode::Single)
        buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    return buffer;
}

void CommandPool::reset() const {
    vk::CommandPoolResetFlags flags = {};
    if (mode_ == UseMode::Single)
        flags |= vk::CommandPoolResetFlagBits::eReleaseResources;
    device_.resetCommandPool(*pool, flags);
}

void CommandPool::free(vk::CommandBuffer buffer) const { device_.freeCommandBuffers(*pool, buffer); }

void CommandPool::freeAll() const { device_.resetCommandPool(*pool, vk::CommandPoolResetFlagBits::eReleaseResources); }

void CommandPool::submit(vk::CommandBuffer buffer) const {
    if (mode_ == UseMode::Single)
        buffer.end();

    queue_.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &buffer});
}

void CommandPool::submit(vk::CommandBuffer buffer, vk::Fence fence) const {
    if (mode_ == UseMode::Single)
        buffer.end();

    queue_.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &buffer}, fence);
}

void CommandPool::submitAndWait(vk::CommandBuffer buffer) {
    if (mode_ == UseMode::Single)
        buffer.end();

    queue_.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &buffer}, *fence_);

    while (device_.waitForFences(*fence_, true, UINT64_MAX) == vk::Result::eTimeout) {
    }
    device_.resetFences(*fence_);
}


Commands::Commands(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode)
    : device_(device), queue_(queue), mode_(mode), trash(device) {
    vk::CommandPoolCreateFlags flags = {};
    if (mode == UseMode::Single || mode == UseMode::Reset)
        flags |= vk::CommandPoolCreateFlagBits::eTransient;
    pool_ = device.createCommandPoolUnique({.flags = flags, .queueFamilyIndex = queue_index});

    fence_ = device.createFenceUnique({});
}

void Commands::begin() {
    if (!active_) {
        active_ = device_.allocateCommandBuffers({
            .commandPool = *pool_,
            .commandBufferCount = 1,
        })[0];
    }

    vk::CommandBufferUsageFlags flags = {};
    if (mode_ == UseMode::Single)
        flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    active_.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
}

void Commands::reset() {
    vk::CommandPoolResetFlags flags = {};
    if (mode_ == UseMode::Single)
        flags |= vk::CommandPoolResetFlagBits::eReleaseResources;
    device_.resetCommandPool(*pool_, flags);

    active_ = vk::CommandBuffer{};
}

const vk::CommandBuffer *Commands::operator->() const noexcept {
    if (!active_) {
        Logger::error("Command buffer not begun");
    }
    return &active_;
}

vk::CommandBuffer *Commands::operator->() noexcept {
    if (!active_) {
        Logger::error("Command buffer not begun");
    }
    return &active_;
}

const vk::CommandBuffer &Commands::operator*() const noexcept {
    if (!active_) {
        Logger::error("Command buffer not begun");
    }
    return active_;
}

vk::CommandBuffer &Commands::operator*() noexcept {
    if (!active_) {
        Logger::error("Command buffer not begun");
    }
    return active_;
}

vk::CommandBuffer Commands::get() const {
    if (!active_) {
        Logger::error("Command buffer not begun");
    }
    return active_;
}

void Commands::free(vk::CommandBuffer buffer) const {
    if (!buffer)
        return;
    device_.freeCommandBuffers(*pool_, buffer);
}

void Commands::wait(vk::Fence fence, bool reset) const {
    if (!fence)
        return;

    while (device_.waitForFences(fence, true, UINT64_MAX) == vk::Result::eTimeout) {
    }
    if (reset)
        device_.resetFences(fence);
}


vk::CommandBuffer Commands::end() {
    active_.end();
    return std::exchange(active_, {});
}


void Commands::submit() {
    if (!active_) {
        Logger::error("Command buffer not begun");
        return;
    }

    active_.end();
    queue_.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &active_}, *fence_);

    wait(*fence_, true);
    trash.clear();

    if (mode_ == UseMode::Single) {
        free(active_);
        active_ = vk::CommandBuffer{};
    }
}

vk::CommandBuffer Commands::submit(vk::Fence fence) {
    if (!active_) {
        Logger::error("Command buffer not begun");
        return {};
    }

    active_.end();
    queue_.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &active_}, fence);

    return std::exchange(active_, {});
}
