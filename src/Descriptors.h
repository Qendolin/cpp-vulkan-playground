#pragma once

#include <vulkan/vulkan.hpp>

constexpr vk::DescriptorSetLayoutBinding DescriptorBinding(uint32_t index, vk::DescriptorType type, vk::ShaderStageFlags stages, uint32_t count = 1) {
    return {index, type, count, stages};
}

class DescriptorSetLayout {
    template <typename ...Args>
    static constexpr bool areBindingsSortedAndUnique(Args... args) {
        std::array indices{args.binding...};
        return std::ranges::is_sorted(indices) &&
               std::ranges::adjacent_find(indices) == indices.end();
    }

public:
    const std::vector<vk::DescriptorSetLayoutBinding> bindings;

    explicit DescriptorSetLayout(vk::Device device, vk::DescriptorSetLayoutCreateFlags flags, std::convertible_to<vk::DescriptorSetLayoutBinding> auto... args) :
    bindings{args...},
    layout(device.createDescriptorSetLayoutUnique({
        .flags = flags,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    })) {
        if(!areBindingsSortedAndUnique(args...)) {
            Logger::panic("Binding indices must be sorted and unique");
        }
    }

    [[nodiscard]] vk::DescriptorSetLayout get() const {
        return layout.get();
    }

private:
    const vk::UniqueDescriptorSetLayout layout;

};

class DescriptorSet {
    vk::DescriptorSet set;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

public:
    DescriptorSet() = default;

    explicit DescriptorSet(vk::DescriptorSet set, std::vector<vk::DescriptorSetLayoutBinding> &&bindings) : set(set), bindings(std::move(bindings)) {
    }

    DescriptorSet(const DescriptorSet &other) = delete;

    DescriptorSet(DescriptorSet &&other) noexcept
        : set(other.set),
          bindings(std::move(other.bindings)) {
    }

    DescriptorSet &operator=(const DescriptorSet &other) = delete;

    DescriptorSet &operator=(DescriptorSet &&other) noexcept {
        set = other.set;
        bindings = std::move(other.bindings);
        return *this;
    }

    [[nodiscard]] vk::DescriptorSet get() const { return set; }

    [[nodiscard]] std::span<const vk::DescriptorSetLayoutBinding> getBindings() const {
        return bindings;
    }

    [[nodiscard]] vk::WriteDescriptorSet write(uint32_t index) const {
        vk::DescriptorSetLayoutBinding binding = bindings.at(index);
        return {
            .dstSet = set,
            .dstBinding = index,
            .dstArrayElement = 0,
            .descriptorCount = binding.descriptorCount,
            .descriptorType = binding.descriptorType,
        };
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
            .pPoolSizes = sizes.data()
        });
    }

    DescriptorSet allocate(const DescriptorSetLayout &layout) {
        vk::DescriptorSetLayout vk_layout = layout.get();
        vk::DescriptorSet set = device.allocateDescriptorSets({
            .descriptorPool = *pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk_layout
        }).at(0);
        return DescriptorSet(set, std::vector(layout.bindings));
    }

private:
    vk::UniqueDescriptorPool pool;
    const vk::Device &device;
};
