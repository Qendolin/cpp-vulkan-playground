#pragma once
#include <memory>
#include <string>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_shared.hpp>

class ShaderCompiler;

struct ShaderStageModule {
    vk::ShaderStageFlagBits stage;
    vk::UniqueShaderModule module;
};

struct ShaderProgramConfig {
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
};

class ShaderLoader {
    vk::SharedDevice device;
    std::shared_ptr<ShaderCompiler> compiler;

public:
    bool optimize = false;
    bool debug = false;
    bool print = false;

    explicit ShaderLoader(vk::SharedDevice device);

    ~ShaderLoader();

    ShaderStageModule load(const std::string &shader_name);

    // TODO: Maybe a builder makes more sense
    std::pair<vk::UniquePipelineLayout, vk::UniquePipeline> link(
        std::span<const vk::Format> color_attachment_fromats, vk::Format depth_attchment_format,
        std::initializer_list<std::reference_wrapper<ShaderStageModule> > stages,
        std::span<const vk::VertexInputBindingDescription> vertex_bindings,
        std::span<const vk::VertexInputAttributeDescription> vertex_attributes,
        std::span<const vk::DescriptorSetLayout> descriptor_set_layouts,
        std::span<const vk::PushConstantRange> push_constant_ranges,
        ShaderProgramConfig config = {});
};
