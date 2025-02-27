#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <vulkan/vulkan.hpp>

class CommandPool {
public:
    enum class UseMode { Single, Reset, ResetIndivitual, Reuse };

private:
    vk::Device device_ = {};
    vk::Queue queue_ = {};
    UseMode mode_ = UseMode::Single;
    vk::UniqueFence fence_ = {};

public:
    vk::UniqueCommandPool pool;

    CommandPool() = default;

    CommandPool(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode);

    [[nodiscard]] vk::CommandBuffer create() const;

    void reset() const;

    void free(vk::CommandBuffer buffer) const;

    void freeAll() const;

    void submit(vk::CommandBuffer buffer) const;

    void submit(vk::CommandBuffer buffer, vk::Fence fence) const;

    void submitAndWait(vk::CommandBuffer buffer);
};

class Trash {
    vk::Device device_;
    std::vector<std::function<void()>> trash_;

public:
    Trash() = default;

    explicit Trash(vk::Device device) : device_(device) {}

    void clear() {
        for (const auto &deleter: trash_)
            deleter();
        trash_.clear();
    }

    template<typename T>
    Trash &operator+=(T &rhs) {
        T val = rhs;
        rhs = T{};
        using deleter_t = typename vk::UniqueHandleTraits<T, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>::deleter;
        if constexpr (std::is_same_v<deleter_t, vk::ObjectDestroy<vk::Device, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>>) {
            trash_.emplace_back([this, val] { device_.destroy(val); });
        } else if constexpr (std::is_same_v<deleter_t, vk::ObjectFree<vk::Device, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>>) {
            trash_.emplace_back([this, val] { device_.free(val); });
        } else {
            static_assert(false, "Unsupported type");
        }
        return *this;
    }
};

class Commands {
public:
    enum class UseMode { Single, Reset, Reuse };

private:
    vk::Device device_ = {};
    vk::Queue queue_ = {};
    UseMode mode_ = UseMode::Single;
    vk::UniqueFence fence_ = {};
    vk::UniqueCommandPool pool_;
    vk::CommandBuffer active_;

public:
    Trash trash;

    Commands() = default;

    Commands(vk::Device device, vk::Queue queue, uint32_t queue_index, UseMode mode);

    void begin();

    vk::CommandBuffer end();

    void submit();

    [[nodiscard]] vk::CommandBuffer submit(vk::Fence fence);

    void wait(vk::Fence fence, bool reset) const;

    void free(vk::CommandBuffer buffer) const;

    void reset();

    const vk::CommandBuffer *operator->() const noexcept;

    vk::CommandBuffer *operator->() noexcept;

    const vk::CommandBuffer &operator*() const noexcept;

    vk::CommandBuffer &operator*() noexcept;

    [[nodiscard]] vk::CommandBuffer get() const;
};
