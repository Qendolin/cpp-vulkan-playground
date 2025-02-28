#include "Framebuffer.h"

void Attachment::barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end) {
    ImageResource::barrier(image, range, cmd_buf, begin, end);
}

void Attachment::barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single) {
    barrier(cmd_buf, single, single);
}

vk::RenderingInfo Framebuffer::renderingInfo(const vk::Rect2D &area, const FramebufferRenderingConfig &config) {
    vk::RenderingInfo result = {
        .flags = config.flags,
        .renderArea = area,
        .layerCount = config.layerCount,
        .viewMask = config.viewMask,
    };

    for (size_t i = 0; i < colorAttachments.size(); i++) {
        const auto &attachment = colorAttachments[i];
        bool enabled = i < config.enabledColorAttachments.size() ? config.enabledColorAttachments[i] : true;
        auto clearColor = i < config.clearColors.size() ? config.clearColors[i] : vk::ClearColorValue{};
        auto loadOp = i < config.colorLoadOps.size() ? config.colorLoadOps[i] : vk::AttachmentLoadOp::eLoad;
        auto storeOp = i < config.colorStoreOps.size() ? config.colorStoreOps[i] : vk::AttachmentStoreOp::eStore;
        if (attachment && enabled) {
            colorAttachmentInfos_[i] = {
                .imageView = attachment.view,
                .imageLayout = vk::ImageLayout::eAttachmentOptimal,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = {.color = clearColor},
            };
        } else {
            colorAttachmentInfos_[i] = {};
        }
    }
    result.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    result.pColorAttachments = colorAttachmentInfos_.data();

    if (depthAttachment && config.enableDepthAttachment) {
        depthAttachmentInfo_ = {
            .imageView = depthAttachment.view,
            .imageLayout = vk::ImageLayout::eAttachmentOptimal,
            .resolveMode = {},
            .resolveImageView = {},
            .resolveImageLayout = {},
            .loadOp = config.depthLoadOp,
            .storeOp = config.depthStoreOp,
            .clearValue = {.depthStencil = {config.clearDepth, config.clearStencil}},
        };
        result.pDepthAttachment = &depthAttachmentInfo_;
    }

    if (stencilAttachment && config.enableDepthAttachment) {
        stencilAttachmentInfo_ = {
            .imageView = stencilAttachment.view,
            .imageLayout = vk::ImageLayout::eAttachmentOptimal,
            .resolveMode = {},
            .resolveImageView = {},
            .resolveImageLayout = {},
            .loadOp = config.stencilLoadOp,
            .storeOp = config.stencilStoreOp,
            .clearValue = {.depthStencil = {config.clearDepth, config.clearStencil}},
        };
        result.pStencilAttachment = &stencilAttachmentInfo_;
    }

    return result;
}

void Framebuffer::barrierColor(
        const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end
) {
    for (auto &attachment: colorAttachments) {
        if (!attachment)
            continue;
        attachment.barrier(cmd_buf, begin, end);
    }
}

void Framebuffer::barrierColor(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single) {
    for (auto &attachment: colorAttachments) {
        if (!attachment)
            continue;
        attachment.barrier(cmd_buf, single);
    }
}

void Framebuffer::barrierDepth(
        const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end
) {
    if (!depthAttachment)
        return;

    depthAttachment.barrier(cmd_buf, begin, end);
}

void Framebuffer::barrierDepth(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single) {
    if (!depthAttachment)
        return;

    depthAttachment.barrier(cmd_buf, single);
}

void Framebuffer::barrierStencil(
        const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end
) {
    if (!stencilAttachment)
        return;

    stencilAttachment.barrier(cmd_buf, begin, end);
}

void Framebuffer::barrierStencil(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single) {
    if (!stencilAttachment)
        return;

    stencilAttachment.barrier(cmd_buf, single);
}
