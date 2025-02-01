#pragma once

#include <vulkan/vulkan.hpp>

template<uint32_t Index, vk::DescriptorType Type, vk::ShaderStageFlagBits Stages = vk::ShaderStageFlagBits::eAll, uint32_t Count = 1>
struct DescriptorBinding {
    constexpr DescriptorBinding() = default;

    const uint32_t index = Index;
    const uint32_t count = Count;
    const vk::DescriptorType type = Type;
    const vk::ShaderStageFlags stages = Stages;

    [[nodiscard]] constexpr vk::DescriptorSetLayoutBinding get() const {
        return {index, type, count, stages};
    }
};

template<typename T>
concept DescriptorBindingType = requires(T t)
{
    { t.index } -> std::convertible_to<uint32_t>;
    { t.count } -> std::convertible_to<uint32_t>;
    { t.type } -> std::convertible_to<vk::DescriptorType>;
    { t.stages } -> std::convertible_to<vk::ShaderStageFlags>;
    { t.get() } -> std::convertible_to<vk::DescriptorSetLayoutBinding>;
};

template<DescriptorBindingType... Bindings>
class DescriptorSetLayout {
public:
    using BindingTypes = std::tuple<Bindings...>;
    static constexpr size_t binding_count = sizeof...(Bindings);

    const std::array<vk::DescriptorSetLayoutBinding, sizeof...(Bindings)> bindings = std::array{Bindings{}.get()...};

private:
    static constexpr bool hasUniqueBindings() {
        constexpr std::array indices{Bindings{}.index...};
        return std::ranges::is_sorted(indices) &&
               std::ranges::adjacent_find(indices) == indices.end();
    }

    static_assert(hasUniqueBindings(), "Binding indices must be sorted and unique");

    vk::UniqueDescriptorSetLayout create(const vk::Device &device, vk::DescriptorSetLayoutCreateFlags flags) {
        return device.createDescriptorSetLayoutUnique({
            .flags = flags,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        });
    }

    const vk::UniqueDescriptorSetLayout layout;

public:
    DescriptorSetLayout(const vk::Device &device, vk::DescriptorSetLayoutCreateFlags flags)
        : layout(create(device, flags)) {
    }

    [[nodiscard]] vk::DescriptorSetLayout get() const {
        return layout.get();
    }
};

template<typename T>
concept DescriptorSetLayoutType = requires(T t)
{
    typename T::BindingTypes;
    { T::binding_count } -> std::convertible_to<size_t>;
    { t.bindings } -> std::same_as<const std::array<vk::DescriptorSetLayoutBinding, T::binding_count> &>;
    { t.get() } -> std::convertible_to<vk::DescriptorSetLayout>;
};

// template<DescriptorSetLayoutType Layout>
// class DescriptorSet {
//     vk::UniqueDescriptorSet descriptor_set;
//
// public:
//     explicit DescriptorSet(vk::UniqueDescriptorSet&& set) : descriptor_set(std::move(set)) {
//     }
//
//     [[nodiscard]] vk::DescriptorSet get() const { return descriptor_set.get(); }
//
//     // Helper to get binding information
//     template<size_t Index>
//     [[nodiscard]] auto get() const {
//         using BindingType = std::tuple_element_t<Index, typename Layout::BindingTypes>;
//         return BindingType{};
//     }
//
//     template<size_t Index>
//     constexpr vk::WriteDescriptorSet write() {
//         auto binding = get<Index>();
//         return {
//             .dstSet = descriptor_set.get(),
//             .dstBinding = binding.index,
//             .dstArrayElement = 0,
//             .descriptorCount = binding.count,
//             .descriptorType = binding.type,
//         };
//     }
// };

struct DescriptorBindingInfo {
    uint32_t binding;
    uint32_t count;
    vk::DescriptorType type;
    vk::ShaderStageFlags stages;
};

class DescriptorSet {
    vk::DescriptorSet set;
    std::vector<DescriptorBindingInfo> bindings;

public:
    DescriptorSet() = default;

    explicit DescriptorSet(vk::DescriptorSet set, std::vector<DescriptorBindingInfo> &&bindings) : set(set), bindings(std::move(bindings)) {
    }

    DescriptorSet(const DescriptorSet &other) = delete;

    DescriptorSet(DescriptorSet &&other) noexcept
        : set(std::move(other.set)),
          bindings(std::move(other.bindings)) {
    }

    DescriptorSet &operator=(const DescriptorSet &other) = delete;

    DescriptorSet &operator=(DescriptorSet &&other) noexcept {
        set = std::move(other.set);
        bindings = std::move(other.bindings);
        return *this;
    }

    [[nodiscard]] vk::DescriptorSet get() const { return set; }

    [[nodiscard]] std::span<const DescriptorBindingInfo> getBindings() const {
        return bindings;
    }

    [[nodiscard]] vk::WriteDescriptorSet write(uint32_t index) const {
        DescriptorBindingInfo binding = bindings.at(index);
        return {
            .dstSet = set,
            .dstBinding = index,
            .dstArrayElement = 0,
            .descriptorCount = binding.count,
            .descriptorType = binding.type,
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

        pool = device.createDescriptorPoolUnique({
            .maxSets = 1024,
            .poolSizeCount = static_cast<uint32_t>(sizes.size()),
            .pPoolSizes = sizes.data()
        });
    }

    template<DescriptorSetLayoutType Layout>
    DescriptorSet allocate(const Layout &layout) {
        auto make_runtime_bindings = []<typename... Bindings>(std::tuple<Bindings...>) {
            return std::vector<DescriptorBindingInfo>{
                DescriptorBindingInfo{
                    Bindings{}.index,
                    Bindings{}.count,
                    Bindings{}.type,
                    Bindings{}.stages
                }...
            };
        };
        auto bindings = make_runtime_bindings(typename Layout::BindingTypes{});

        vk::DescriptorSetLayout vk_layout = layout.get();
        vk::DescriptorSet set = device.allocateDescriptorSets({
            .descriptorPool = *pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk_layout
        }).at(0);
        return DescriptorSet(set, std::move(bindings));
    }

private:
    vk::UniqueDescriptorPool pool;
    const vk::Device &device;
};
