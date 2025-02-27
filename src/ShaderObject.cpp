#include "ShaderObject.h"

#include <ranges>

#include "Logger.h"

ShaderStage::ShaderStage(std::string_view name, vk::ShaderStageFlagBits stage, vk::ShaderCreateFlagsEXT flags,
                         std::vector<uint32_t> &&code): code(std::move(code)), name(name) {
    create_info = {
        .flags = flags,
        .stage = stage,
        .codeType = vk::ShaderCodeTypeEXT::eSpirv,
        .codeSize = this->code.size() * sizeof(this->code[0]),
        .pCode = this->code.data(),
        .pName = "main",
    };
}

void PipelineConfig::apply(const vk::CommandBuffer &cmd_buf, vk::ShaderStageFlags stages) const {
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

std::vector<vk::ShaderCreateInfoEXT> Shader::chainStages(std::initializer_list<ShaderStage> stages) {
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

Shader::Shader(const vk::Device &device, std::vector<vk::ShaderCreateInfoEXT> shader_create_infos,
               std::span<const vk::DescriptorSetLayout> descriptor_set_layouts, std::span<const vk::PushConstantRange> push_constant_ranges) {
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

void Shader::bindDescriptorSet(vk::CommandBuffer command_buffer, int index, vk::DescriptorSet set, vk::ArrayProxy<const uint32_t> const &dynamicOffsets) const {
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, index, set, dynamicOffsets);
}

ShaderStage ShaderLoader::load(const std::filesystem::path &path, vk::ShaderCreateFlagBitsEXT flags) const {
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
