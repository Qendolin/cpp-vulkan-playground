#pragma once

#include <vulkan/vulkan.hpp>

class CommandPool {
public:
    enum class UseMode {
        Single, Reset, Reuse
    };

private:
    vk::Device device = {};
    vk::Queue queue = {};
    UseMode mode = UseMode::Single;
    vk::UniqueFence fence;

public:
    vk::UniqueCommandPool pool;

    CommandPool(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode);

    vk::CommandBuffer create();

    void reset();

    void freeAll();

    void submit(vk::CommandBuffer buffer);

    void submitAndWait(vk::CommandBuffer buffer);
};
