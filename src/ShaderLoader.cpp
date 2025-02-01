#include "ShaderLoader.h"

#include <filesystem>
#include <fstream>

#include "Logger.h"

#include <shaderc/shaderc.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "ShaderCompiler.h"

ShaderLoader::ShaderLoader(vk::SharedDevice device) : device(device) {
    compiler = std::make_unique<ShaderCompiler>();
}

ShaderLoader::~ShaderLoader() = default;

ShaderStageModule ShaderLoader::load(const std::string &shader_name) {
    std::filesystem::path source_path(shader_name);
    vk::ShaderStageFlagBits stage;
    auto ext = source_path.extension().string().substr(1);
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
        Logger::panic("Unknown shader type: " + source_path.string());

    auto binary = compiler->compile(source_path, stage, {optimize, debug, print});

    auto module_create_info = vk::ShaderModuleCreateInfo{
        .codeSize = binary.size() * sizeof(uint32_t),
        .pCode = binary.data(),
    };

    auto shader_module = device->createShaderModuleUnique(module_create_info);

    return {stage, std::move(shader_module)};
}

std::pair<vk::UniquePipelineLayout, vk::UniquePipeline> ShaderLoader::link(
    vk::RenderPass &render_pass, std::initializer_list<std::reference_wrapper<ShaderStageModule> > stages,
    std::span<const vk::VertexInputBindingDescription> vertex_bindings,
    std::span<const vk::VertexInputAttributeDescription> vertex_attributes,
    std::span<const vk::DescriptorSetLayout> descriptor_set_layouts,
    std::span<const vk::PushConstantRange> push_constant_ranges,
    ShaderProgramConfig config) {
    std::vector<vk::PipelineShaderStageCreateInfo> stage_create_infos;
    stage_create_infos.reserve(stages.size());

    for (auto stage: stages) {
        stage_create_infos.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = stage.get().stage,
            .module = *stage.get().module,
            .pName = "main",
        });
    }

    std::array dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eBlendConstants,
        vk::DynamicState::eStencilCompareMask,
        vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilReference,
        vk::DynamicState::eCullMode,
        vk::DynamicState::eDepthTestEnable,
        vk::DynamicState::eDepthWriteEnable,
        vk::DynamicState::eDepthCompareOp,
        vk::DynamicState::eStencilTestEnable,
        vk::DynamicState::eStencilOp,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .dynamicStateCount = dynamic_states.size(),
        .pDynamicStates = dynamic_states.data()
    };

    vk::PipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_bindings.size()),
        .pVertexBindingDescriptions = vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size()),
        .pVertexAttributeDescriptions = vertex_attributes.data()
    };

    vk::PipelineInputAssemblyStateCreateInfo assembly_state_create_info = {
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = false
    };

    vk::PipelineViewportStateCreateInfo viewport_state_create_info = {
        .viewportCount = 1,
        .scissorCount = 1,
    };

    vk::PipelineRasterizationStateCreateInfo rasterization_state_create_info = {
        .depthClampEnable = config.depthClampEnable,
        .rasterizerDiscardEnable = config.rasterizerDiscardEnable,
        .polygonMode = config.polygonMode,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = config.depthBiasEnable,
        .depthBiasConstantFactor = config.depthBiasConstantFactor,
        .depthBiasClamp = config.depthBiasClamp,
        .depthBiasSlopeFactor = config.depthBiasSlopeFactor,
        .lineWidth = 1.0f,
    };

    vk::PipelineMultisampleStateCreateInfo multisample_state_create_info{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = false,
        .alphaToOneEnable = false,
    };

    vk::PipelineColorBlendAttachmentState color_blend_attachment_state = {
        .blendEnable = false,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eZero,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo color_blend_state_create_info = {
        .logicOpEnable = false,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment_state,
        .blendConstants = vk::ArrayWrapper1D<float, 4>({0.0f, 0.0f, 0.0f, 0.0f})
    };

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthCompareOp = vk::CompareOp::eLess,
        .stencilTestEnable = false,
    };

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info = {
        .setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
        .pSetLayouts = descriptor_set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constant_ranges.size()),
        .pPushConstantRanges = push_constant_ranges.data()
    };

    vk::UniquePipelineLayout pipeline_layout = device->createPipelineLayoutUnique(pipeline_layout_create_info);

    vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .stageCount = static_cast<uint32_t>(stage_create_infos.size()),
        .pStages = stage_create_infos.data(),
        .pVertexInputState = &vertex_input_state_create_info,
        .pInputAssemblyState = &assembly_state_create_info,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &multisample_state_create_info,
        .pDepthStencilState = &depth_stencil_state_create_info,
        .pColorBlendState = &color_blend_state_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = *pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };

    auto pipeline = device->createGraphicsPipelineUnique(nullptr, graphics_pipeline_create_info).value;
    return std::make_pair(std::move(pipeline_layout), std::move(pipeline));
}
