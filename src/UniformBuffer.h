#pragma once

// #include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

template<typename T>
class UnifromBuffer {
    size_t count_;
    vma::UniqueBuffer buffer_;
    vma::UniqueAllocation allocation_;
    T *data_;

public:
    explicit UnifromBuffer(const vma::Allocator &allocator, size_t count = 1) : count_(count) {
        vma::AllocationInfo allocation_result = {};
        std::tie(buffer_, allocation_) = allocator.createBufferUnique(
                {
                    .size = sizeof(T) * count,
                    .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
                },
                {
                    .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                    .usage = vma::MemoryUsage::eAuto,
                    .requiredFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                    .preferredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
                },
                &allocation_result
        );
        data_ = static_cast<T *>(allocation_result.pMappedData);
    }

    UnifromBuffer(const UnifromBuffer &other) = delete;

    UnifromBuffer(UnifromBuffer &&other) noexcept
        : count_(std::exchange(other.count_, 0)),
          buffer_(std::move(other.buffer_)),
          allocation_(std::move(other.allocation_)),
          data_(std::exchange(other.data_, nullptr)) {}

    UnifromBuffer &operator=(const UnifromBuffer &other) = delete;

    UnifromBuffer &operator=(UnifromBuffer &&other) noexcept {
        if (this == &other)
            return *this;
        count_ = std::exchange(other.count_, 0);
        buffer_ = std::move(other.buffer_);
        allocation_ = std::move(other.allocation_);
        data_ = std::exchange(other.data_, nullptr);
        return *this;
    }

    [[nodiscard]] vk::Buffer buffer() const { return *buffer_; }

    [[nodiscard]] T &front() { return data_[0]; }

    [[nodiscard]] const T &front() const { return data_[0]; }

    [[nodiscard]] T &operator[](size_t index) { return data_[index]; }
    [[nodiscard]] const T &operator[](size_t index) const { return data_[index]; }

    [[nodiscard]] T &at(size_t index) {
        if (index >= count_)
            throw std::out_of_range("Index out of range");
        return data_[index];
    }

    [[nodiscard]] const T &at(size_t index) const {
        if (index >= count_)
            throw std::out_of_range("Index out of range");
        return data_[index];
    }
};
