#pragma once
#include <array>
#include <tuple>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan.hpp>

#include "CommandPool.h"

class Commands;

class IStagingBuffer {
    static void copyAndDiscard(Commands &commands, vk::Buffer staging, vk::Buffer dst, size_t size);

public:
    virtual ~IStagingBuffer() = default;

    [[nodiscard]] virtual std::tuple<vk::Buffer, void *> allocate(Commands &commands, size_t size) = 0;

    [[nodiscard]] std::tuple<vk::Buffer, void *> upload(Commands &commands, size_t size, const void *data);

    template<std::ranges::contiguous_range R>
    [[nodiscard]] std::tuple<vk::Buffer, void *> upload(Commands &commands, R &&data) {
        using T = std::ranges::range_value_t<R>;
        return upload(commands, data.size() * sizeof(T), data.data());
    }

    template<std::ranges::contiguous_range R>
    void upload(Commands &commands, R &&data, vk::Buffer dst) {
        using T = std::ranges::range_value_t<R>;
        auto [buffer, ptr] = upload(commands, std::forward<R>(data));
        copyAndDiscard(commands, buffer, dst, data.size() * sizeof(T));
    }

    [[nodiscard]] virtual vma::Allocator allocator() const = 0;
};

class DoubleStagingBuffer : public IStagingBuffer {
    struct Buffer {
        vma::UniqueBuffer buffer = {};
        vma::UniqueAllocation allocation = {};
        void *data = nullptr;
        size_t offset = 0;
        std::vector<vk::UniqueBuffer> buffers;
        vk::UniqueFence fence = {};
        vk::CommandBuffer pendingCommandBuffer = {};
    };

    vma::UniqueAllocation oversizeBufferAllocation_ = {};

    vma::Allocator allocator_ = {};
    size_t capacity_ = 0;
    size_t index_ = 0;
    std::array<Buffer, 2> buffers_ = {};
    Buffer *current_ = &buffers_[0];
    vk::DeviceSize alignment_ = 0;

    [[nodiscard]] std::pair<vma::UniqueBuffer, vma::UniqueAllocation> createHostVisibleBuffer(
            size_t size, vma::AllocationInfo *result_info, bool canAlias
    ) const;

    void swap(const Commands &commands);

    size_t alignOffset(size_t offset) const;

public:
    ~DoubleStagingBuffer() override = default;

    DoubleStagingBuffer(const vma::Allocator &allocator, const vk::Device &device, size_t capacity);

    [[nodiscard]] std::tuple<vk::Buffer, void *> allocate(Commands &commands, size_t size) override;

    [[nodiscard]] vma::Allocator allocator() const override { return allocator_; }
};
