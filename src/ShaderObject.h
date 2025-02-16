#pragma once

#include <ranges>
#include <vulkan/vulkan.hpp>

#include "Logger.h"
#include "ShaderCompiler.h"
#include "util/static_vector.h"

class ShaderStage {
    vk::ShaderCreateInfoEXT create_info;
    const std::vector<uint32_t> code;
    const std::string name;

public:
    ShaderStage(std::string_view name, vk::ShaderStageFlagBits stage, vk::ShaderCreateFlagsEXT flags,
                std::vector<uint32_t> &&code) : code(std::move(code)), name(name) {
        create_info = {
            .flags = flags,
            .stage = stage,
            .codeType = vk::ShaderCodeTypeEXT::eSpirv,
            .codeSize = this->code.size() * sizeof(this->code[0]),
            .pCode = this->code.data(),
            .pName = "main",
        };
    }

    [[nodiscard]] vk::ShaderCreateInfoEXT createInfo() const {
        return create_info;
    }
};

struct StencilOpConfig {
    vk::StencilFaceFlagBits faceMask = vk::StencilFaceFlagBits::eFrontAndBack;
    vk::StencilOp failOp = vk::StencilOp::eKeep;
    vk::StencilOp passOp = vk::StencilOp::eKeep;
    vk::StencilOp depthFailOp = vk::StencilOp::eKeep;
    vk::CompareOp compareOp = vk::CompareOp::eNever;
};

struct StencilCompareMaskConfig {
    vk::StencilFaceFlagBits faceMask = vk::StencilFaceFlagBits::eFrontAndBack;
    uint32_t compareMask = 0;
};

struct StencilWriteMaskConfig {
    vk::StencilFaceFlagBits faceMask = vk::StencilFaceFlagBits::eFrontAndBack;
    uint32_t writeMask = 0;
};

struct StencilReferenceConfig {
    vk::StencilFaceFlagBits faceMask = vk::StencilFaceFlagBits::eFrontAndBack;
    uint32_t reference = 0;
};

struct PipelineConfig {
private:
    static constexpr std::array<uint32_t, 1> DEFAULT_SAMPLE_MASK = {-1u};
    static constexpr std::array<vk::Bool32, 1> DEFAULT_COLOR_BLEND_ENABLE = {false};
    static constexpr std::array<vk::ColorComponentFlags, 1> DEFAULT_COLOR_WRITE_MASK = {
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    static constexpr std::array<vk::ColorBlendEquationEXT, 1> DEFAULT_COLOR_BLEND_EQUATIONS = {
        vk::ColorBlendEquationEXT{
            .srcColorBlendFactor = vk::BlendFactor::eOne, .dstColorBlendFactor = vk::BlendFactor::eZero, .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne, .dstAlphaBlendFactor = vk::BlendFactor::eZero, .alphaBlendOp = vk::BlendOp::eAdd
        }
    };

public:
    // vertex config
    std::span<const vk::VertexInputBindingDescription2EXT> vertexBindingDescriptions = {};
    std::span<const vk::VertexInputAttributeDescription2EXT> vertexAttributeDescriptions = {};
    vk::PrimitiveTopology primitiveTopology = vk::PrimitiveTopology::eTriangleList;
    bool primitiveRestartEnable = false;

    // raster config
    util::static_vector<vk::Viewport, 8> viewports = {};
    util::static_vector<vk::Rect2D, 8> scissors = {};
    bool rasterizerDiscardEnable = false;
    vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;
    util::static_vector<vk::SampleMask, 32> sampleMask = DEFAULT_SAMPLE_MASK;
    bool alphaToCoverageEnable = false;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    float lineWidth = 1.0f;
    vk::LineRasterizationModeEXT lineRasterizationMode = vk::LineRasterizationModeEXT::eDefault;
    bool lineStippleEnable = false;
    uint32_t lineStippleFactor = 0;
    uint16_t lineStipplePattern = 0;
    vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eClockwise;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
    bool depthBoundsTestEnable = false;
    std::pair<float, float> depthBounds = {0.0f, 1.0f};
    bool depthBiasEnable = false;
    vk::DepthBiasInfoEXT depthBias = {};
    bool depthClampEnable = true;
    bool stencilTestEnable = false;
    StencilOpConfig stencilOp = {};
    StencilCompareMaskConfig stencilCompareMask = {};
    StencilWriteMaskConfig stencilWriteMask = {};
    StencilReferenceConfig stencilReference = {};

    // fragment config
    util::static_vector<vk::Bool32, 32> colorBlendEnable = DEFAULT_COLOR_BLEND_ENABLE;
    util::static_vector<vk::ColorBlendEquationEXT, 32> colorBlendEquations = DEFAULT_COLOR_BLEND_EQUATIONS;
    std::array<float, 4> blendConstants = {0, 0, 0, 0};
    util::static_vector<vk::ColorComponentFlags, 32> colorWriteMask = DEFAULT_COLOR_WRITE_MASK;

    void apply(const vk::CommandBuffer &cmd_buf, vk::ShaderStageFlags stages) const {
        if (stages & vk::ShaderStageFlagBits::eVertex) {
            Logger::check(!vertexBindingDescriptions.empty() && !vertexAttributeDescriptions.empty(), "No vertex bindings or attributes in pipeline config!");
            cmd_buf.setVertexInputEXT(vertexBindingDescriptions, vertexAttributeDescriptions);
            cmd_buf.setPrimitiveTopology(primitiveTopology);
            cmd_buf.setPrimitiveRestartEnable(primitiveRestartEnable);
        }

        if (viewports.empty()) {
            Logger::check(!viewports.empty(), "No viewports in pipeline config!");
        }
        cmd_buf.setViewportWithCount(viewports);
        if (scissors.empty()) {
            Logger::check(!scissors.empty(), "No scissor regions in pipeline config!");
        }
        cmd_buf.setScissorWithCount(scissors);
        cmd_buf.setRasterizerDiscardEnable(rasterizerDiscardEnable);

        // TODO: tese, tesc
        if (!rasterizerDiscardEnable) {
            cmd_buf.setRasterizationSamplesEXT(rasterizationSamples);
            cmd_buf.setSampleMaskEXT(rasterizationSamples, sampleMask);
            cmd_buf.setAlphaToCoverageEnableEXT(alphaToCoverageEnable);
            cmd_buf.setPolygonModeEXT(polygonMode);
            if (polygonMode == vk::PolygonMode::eLine) {
                cmd_buf.setLineRasterizationModeEXT(lineRasterizationMode);
                cmd_buf.setLineStippleEnableEXT(lineStippleEnable);
                cmd_buf.setLineStippleEXT(lineStippleFactor, lineStipplePattern);
            }
            cmd_buf.setCullMode(cullMode);
            cmd_buf.setFrontFace(frontFace);
            cmd_buf.setDepthTestEnable(depthTestEnable);
            cmd_buf.setDepthWriteEnable(depthWriteEnable);
            if (depthTestEnable) {
                cmd_buf.setDepthCompareOp(depthCompareOp);
            }
            cmd_buf.setDepthBoundsTestEnable(depthBoundsTestEnable);
            if (depthBoundsTestEnable) {
                cmd_buf.setDepthBounds(depthBounds.first, depthBounds.second);
            }
            cmd_buf.setDepthBiasEnable(depthBiasEnable);
            if (depthBiasEnable) {
                cmd_buf.setDepthBias2EXT(depthBias);
            }
            cmd_buf.setDepthClampEnableEXT(depthClampEnable);
            cmd_buf.setStencilTestEnable(stencilTestEnable);
            if (stencilTestEnable) {
                cmd_buf.setStencilOp(stencilOp.faceMask, stencilOp.failOp, stencilOp.passOp, stencilOp.depthFailOp, stencilOp.compareOp);
                cmd_buf.setStencilCompareMask(stencilCompareMask.faceMask, stencilCompareMask.compareMask);
                cmd_buf.setStencilWriteMask(stencilWriteMask.faceMask, stencilWriteMask.writeMask);
                cmd_buf.setStencilReference(stencilReference.faceMask, stencilReference.reference);
            }

            if (stages & vk::ShaderStageFlagBits::eFragment) {
                cmd_buf.setLogicOpEnableEXT(false);
                cmd_buf.setColorBlendEnableEXT(0, colorBlendEnable);
                if (true) {
                    cmd_buf.setColorBlendEquationEXT(0, colorBlendEquations);
                    cmd_buf.setBlendConstants(blendConstants.data());
                }
                cmd_buf.setColorWriteMaskEXT(0, colorWriteMask);
            }
        }
    }
};

class Shader {
    std::vector<vk::UniqueShaderEXT> handles;
    std::vector<vk::ShaderEXT> view;
    std::vector<vk::ShaderStageFlagBits> stages_;
    vk::ShaderStageFlags stageFlags_ = {};

    vk::UniquePipelineLayout pipeline_layout;

    static std::vector<vk::ShaderCreateInfoEXT> chainStages(std::initializer_list<ShaderStage> stages) {
        std::vector<vk::ShaderCreateInfoEXT> create_infos;
        std::transform(stages.begin(), stages.end(), std::back_inserter(create_infos),
                       [](const ShaderStage &s) { return s.createInfo(); });
        for (size_t i = 0; i < create_infos.size(); ++i) {
            if (i + 1 < create_infos.size())
                create_infos[i].nextStage = create_infos[i + 1].stage;
            create_infos[i].flags |= vk::ShaderCreateFlagBitsEXT::eLinkStage;
        }
        return create_infos;
    }

    Shader(const vk::Device &device, std::vector<vk::ShaderCreateInfoEXT> shader_create_infos, std::span<const vk::DescriptorSetLayout> descriptor_set_layouts,
           std::span<const vk::PushConstantRange> push_constant_ranges) {
        for (auto &info: shader_create_infos) {
            stageFlags_ |= info.stage;
            info.setSetLayouts(descriptor_set_layouts);
            info.setPushConstantRanges(push_constant_ranges);
        }
        handles = device.createShadersEXTUnique(shader_create_infos).value;
        view = std::ranges::transform_view(handles, [](auto &u) { return u.get(); }) | std::ranges::to<std::vector>();
        stages_ = std::ranges::transform_view(shader_create_infos, [](auto &u) { return u.stage; }) | std::ranges::to<std::vector>();

        pipeline_layout = device.createPipelineLayoutUnique({
            .setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
            .pSetLayouts = descriptor_set_layouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(push_constant_ranges.size()),
            .pPushConstantRanges = push_constant_ranges.data()
        });
    }

public:
    Shader(const vk::Device &device, std::initializer_list<ShaderStage> stages, std::span<const vk::DescriptorSetLayout> descriptor_set_layouts = {},
           std::span<const vk::PushConstantRange> push_constant_ranges = {})
        : Shader(device, chainStages(stages), descriptor_set_layouts, push_constant_ranges) {
    }

    Shader(const vk::Device &device, const ShaderStage &stage, vk::ShaderStageFlagBits next_stages,
           std::span<const vk::DescriptorSetLayout> descriptor_set_layouts = {}, std::span<const vk::PushConstantRange> push_constant_ranges = {})
        : Shader(device, {stage.createInfo().setNextStage(next_stages)}, descriptor_set_layouts, push_constant_ranges) {
    }

    [[nodiscard]] std::span<const vk::ShaderStageFlagBits> stages() const {
        return stages_;
    }

    [[nodiscard]] vk::ShaderStageFlags stageFlags() const {
        return stageFlags_;
    }

    [[nodiscard]] std::span<const vk::ShaderEXT> shaders() const {
        return view;
    }

    [[nodiscard]] vk::PipelineLayout pipelineLayout() const {
        return *pipeline_layout;
    }

    void bindDescriptorSet(vk::CommandBuffer command_buffer, int index, vk::DescriptorSet set, vk::ArrayProxy<const uint32_t> const &dynamicOffsets = {}) const {
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, index, set, dynamicOffsets);
    }
};

class ShaderLoader {
    std::shared_ptr<ShaderCompiler> compiler;

public:
    bool optimize = false;
    bool debug = false;
    bool print = false;


    ShaderLoader() {
        compiler = std::make_unique<ShaderCompiler>();
    }

    [[nodiscard]] ShaderStage load(const std::filesystem::path &path, vk::ShaderCreateFlagBitsEXT flags = {}) const {
        vk::ShaderStageFlagBits stage;
        auto ext = path.extension().string().substr(1);
        if (ext == "vert")
            stage = vk::ShaderStageFlagBits::eVertex;
        else if (ext == "tesc")
            stage = vk::ShaderStageFlagBits::eTessellationControl;
        else if (ext == "tese")
            stage = vk::ShaderStageFlagBits::eTessellationEvaluation;
        else if (ext == "geom")
            stage = vk::ShaderStageFlagBits::eGeometry;
        else if (ext == "frag")
            stage = vk::ShaderStageFlagBits::eFragment;
        else if (ext == "comp")
            stage = vk::ShaderStageFlagBits::eCompute;
        else
            Logger::panic("Unknown shader type: " + path.string());

        auto binary = compiler->compile(path, stage, {optimize, debug, print});
        return {path.filename().string(), stage, flags, std::move(binary)};
    }
};
