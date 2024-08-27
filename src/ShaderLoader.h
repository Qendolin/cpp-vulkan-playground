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

    ShaderLoader(vk::SharedDevice device);
    ~ShaderLoader();

    ShaderStageModule load(const std::string& shader_name);

    std::pair<vk::UniquePipelineLayout, vk::UniquePipeline> link(vk::RenderPass& render_pass, std::initializer_list<std::reference_wrapper<ShaderStageModule>> stages, ShaderProgramConfig config);
};
