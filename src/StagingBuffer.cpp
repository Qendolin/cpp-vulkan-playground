#include "StagingBuffer.h"

#include <cstring>
#include <utility>
#include <format>

#include "CommandPool.h"
#include "Logger.h"

void IStagingBuffer::copyAndDiscard(Commands &commands, vk::Buffer staging, vk::Buffer dst, size_t size) {
    commands->copyBuffer(staging, dst, vk::BufferCopy{.size = size});
    commands.trash += staging;
}

std::tuple<vk::Buffer, void *> IStagingBuffer::upload(Commands &commands, size_t size, const void *data) {
    auto result = allocate(commands, size);
    std::memcpy(std::get<1>(result), data, size);
    return result;
}

std::pair<vma::UniqueBuffer, vma::UniqueAllocation> DoubleStagingBuffer::createHostVisibleBuffer(
    size_t size, vma::AllocationInfo *result_info, bool canAlias) const {
    vma::AllocationCreateFlags flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped;
    if (canAlias)
        flags |= vma::AllocationCreateFlagBits::eCanAlias;
    return allocator_.createBufferUnique(
        {
            .size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
        },
        {
            .flags = flags,
            .usage = vma::MemoryUsage::eAuto,
            .requiredFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        }, result_info);
}

DoubleStagingBuffer::DoubleStagingBuffer(const vma::Allocator &allocator, const vk::Device &device, size_t capacity)
    : allocator_(allocator), capacity_(capacity) {
    vma::AllocationInfo allocation_result = {};
    bool first = true;
    for (auto &buffer: buffers_) {
        buffer.fence = device.createFenceUnique({.flags = first ? vk::FenceCreateFlags{} : vk::FenceCreateFlagBits::eSignaled});
        std::tie(buffer.buffer, buffer.allocation) = createHostVisibleBuffer(capacity, &allocation_result, true);
        buffer.data = allocation_result.pMappedData;
        first = false;

        const auto memReq = device.getBufferMemoryRequirements(*buffer.buffer);
        alignment_ = std::max(alignment_, memReq.alignment);
    }
}

void DoubleStagingBuffer::swap(const Commands &commands) {
    index_ = (index_ + 1) % buffers_.size();
    current_ = &buffers_[index_];
    commands.wait(*current_->fence, true);
    commands.free(std::exchange(current_->pendingCommandBuffer, {}));
    current_->offset = 0;
}

// https://stackoverflow.com/a/9194117/7448536
size_t DoubleStagingBuffer::alignOffset(size_t offset) const {
    return (offset + alignment_ - 1) & -alignment_;
}

std::tuple<vk::Buffer, void *> DoubleStagingBuffer::allocate(Commands &commands, size_t size) {
    // handle oversize case
    if (size > capacity_) {
        Logger::warning(std::format("Allocation larger than staging capacity; performance suboptimal; {} bytes over {}", size - capacity_, capacity_));
        // Make sure the old allocation is not in use
        if (oversizeBufferAllocation_) {
            commands.submit();
            commands.begin();
        }
        vma::AllocationInfo allocation_result = {};
        auto [buffer, alloc] = createHostVisibleBuffer(size, &allocation_result, false);
        oversizeBufferAllocation_ = std::move(alloc);
        return {buffer.release(), allocation_result.pMappedData};
    }

    // swap if remaining space is too small
    if (size > capacity_ - current_->offset) {
        current_->pendingCommandBuffer = commands.submit(*current_->fence);
        swap(commands);
        commands.begin();
    }

    std::tuple result = {
        allocator_.createAliasingBuffer2(
            *current_->allocation, current_->offset,
            {
                .size = size,
                .usage = vk::BufferUsageFlagBits::eTransferSrc,
            }),
        static_cast<unsigned char *>(current_->data) + current_->offset
    };
    current_->offset = alignOffset(current_->offset + size);
    return result;
}
