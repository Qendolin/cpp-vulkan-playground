#pragma once

#include <vulkan/vulkan.hpp>

template<vk::DescriptorType Type>
struct DescriptorBinding : vk::DescriptorSetLayoutBinding {

    consteval DescriptorBinding() noexcept = default;

    explicit consteval DescriptorBinding(uint32_t index, uint32_t count, vk::ShaderStageFlags stages) noexcept
        : vk::DescriptorSetLayoutBinding({index, Type, count, stages}) {}

    explicit consteval DescriptorBinding(const vk::DescriptorSetLayoutBinding &binding) noexcept
        : vk::DescriptorSetLayoutBinding(binding) {}
};

class DescriptorSetLayoutBase {
    vk::UniqueDescriptorSetLayout handle_;

public:
    std::span<const vk::DescriptorSetLayoutBinding> bindings;
    vk::DescriptorSetLayout layout;

    virtual ~DescriptorSetLayoutBase() = default;

    DescriptorSetLayoutBase(const DescriptorSetLayoutBase &other)
        : handle_(), bindings(other.bindings), layout(other.layout) {}

    DescriptorSetLayoutBase(DescriptorSetLayoutBase &&other) noexcept
        : handle_(std::move(other.handle_)),
          bindings(std::exchange(other.bindings, {})),
          layout(std::exchange(other.layout, {})) {}

    virtual DescriptorSetLayoutBase &operator=(const DescriptorSetLayoutBase &other) {
        if (this == &other)
            return *this;
        handle_.reset();
        bindings = other.bindings;
        layout = other.layout;
        return *this;
    }
    virtual DescriptorSetLayoutBase &operator=(DescriptorSetLayoutBase &&other) noexcept {
        if (this == &other)
            return *this;
        handle_ = std::move(other.handle_);
        bindings = std::move(other.bindings);
        layout = std::move(other.layout);
        return *this;
    }

protected:
    template<std::size_t N>
    DescriptorSetLayoutBase(
            const vk::Device &device,
            vk::DescriptorSetLayoutCreateFlags flags,
            const std::array<vk::DescriptorSetLayoutBinding, N> &bindings
    )
        : handle_( //
                  device.createDescriptorSetLayoutUnique({.flags = flags, .bindingCount = N, .pBindings = bindings.data()})
          ),
          bindings(bindings),
          layout((assert(handle_), *handle_)) {}

    using Type = vk::DescriptorType;
    using ShaderStage = vk::ShaderStageFlagBits;
    using ShaderStages = vk::ShaderStageFlags;

    template<typename... Args>
    static std::array<vk::DescriptorSetLayoutBinding, sizeof...(Args)> validate(Args... args) {
        auto result = std::array{static_cast<vk::DescriptorSetLayoutBinding>(args)...};
        validateBindings(result);
        return result;
    }

    static consteval vk::DescriptorSetLayoutBinding binding(
            uint32_t index, vk::DescriptorType type, ShaderStages stages, uint32_t count = 1
    ) {
        return {index, type, count, stages};
    }

    static consteval auto combinedImageSampler(uint32_t index, vk::ShaderStageFlags stages, uint32_t count = 1) {
        return DescriptorBinding<vk::DescriptorType::eCombinedImageSampler>(index, count, stages);
    }

    static consteval auto inlineUniformBlock(uint32_t index, vk::ShaderStageFlags stages, uint32_t size) {
        return DescriptorBinding<vk::DescriptorType::eInlineUniformBlock>{index, size, stages};
    }

    static consteval auto uniformBuffer(uint32_t index, ShaderStages stages, uint32_t count = 1) {
        return DescriptorBinding<vk::DescriptorType::eUniformBuffer>{index, count, stages};
    }

private:
    static void validateBindings(std::span<const vk::DescriptorSetLayoutBinding> bindings);
};


struct ShaderInterfaceLayout {
    const std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    const std::vector<vk::PushConstantRange> pushConstantRanges;
};

class DescriptorSet {
public:
    vk::DescriptorSet set;
    std::span<const vk::DescriptorSetLayoutBinding> bindings;

    DescriptorSet() = default;

    explicit DescriptorSet(const vk::DescriptorSet &set, std::span<const vk::DescriptorSetLayoutBinding> bindings)
        : set(set), bindings(bindings) {}

    [[nodiscard]] vk::WriteDescriptorSet write(const vk::DescriptorSetLayoutBinding &binding) const {
        return {
            .dstSet = set,
            .dstBinding = binding.binding,
            .dstArrayElement = 0,
            .descriptorCount = binding.descriptorCount,
            .descriptorType = binding.descriptorType,
        };
    }

    [[nodiscard]] vk::WriteDescriptorSet write(
            const DescriptorBinding<vk::DescriptorType::eCombinedImageSampler> &binding, const vk::DescriptorImageInfo &image_info
    ) const {
        return write(binding).setImageInfo(image_info);
    }

    [[nodiscard]] vk::WriteDescriptorSet write(
            const DescriptorBinding<vk::DescriptorType::eInlineUniformBlock> &binding,
            const vk::WriteDescriptorSetInlineUniformBlock &uniform_block
    ) const {
        return write(binding).setPNext(&uniform_block);
    }

    [[nodiscard]] vk::WriteDescriptorSet write(
            const DescriptorBinding<vk::DescriptorType::eUniformBuffer> &binding, const vk::DescriptorBufferInfo &buffer_info
    ) const {
        return write(binding).setBufferInfo(buffer_info);
    }

    DescriptorSet(const DescriptorSet &other) = default;

    DescriptorSet(DescriptorSet &&other) noexcept
        : set(std::exchange(other.set, {})), bindings(std::exchange(other.bindings, {})) {}

    DescriptorSet &operator=(const DescriptorSet &other) {
        if (this == &other)
            return *this;
        set = other.set;
        bindings = other.bindings;
        return *this;
    }

    DescriptorSet &operator=(DescriptorSet &&other) noexcept {
        if (this == &other)
            return *this;
        set = std::exchange(other.set, {});
        bindings = std::exchange(other.bindings, {});
        return *this;
    }
};

class DescriptorAllocator {
public:
    explicit DescriptorAllocator(const vk::Device &device) : device(device) {
        std::vector<vk::DescriptorPoolSize> sizes = {
            {vk::DescriptorType::eCombinedImageSampler, 1024},
            {vk::DescriptorType::eUniformBuffer, 1024},
        };

        vk::DescriptorPoolInlineUniformBlockCreateInfo uniform_blocks = {
            .maxInlineUniformBlockBindings = 4096,
        };

        pool = device.createDescriptorPoolUnique({
            .pNext = &uniform_blocks,
            .maxSets = 1024,
            .poolSizeCount = static_cast<uint32_t>(sizes.size()),
            .pPoolSizes = sizes.data(),
        });
    }

    DescriptorSet allocate(const DescriptorSetLayoutBase &layout) {
        vk::DescriptorSetAllocateInfo info = {.descriptorPool = *pool, .descriptorSetCount = 1, .pSetLayouts = &layout.layout};
        vk::DescriptorSet set = device.allocateDescriptorSets(info).at(0);
        return DescriptorSet(set, layout.bindings);
    }

private:
    vk::UniqueDescriptorPool pool;
    const vk::Device &device;
};
