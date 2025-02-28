#pragma once

#include <array>

#include "Image.h"
#include "util/static_vector.h"

struct Attachment : ImageResource {
    vk::Image image = {};
    vk::ImageView view = {};
    vk::Format format = {};
    vk::ImageSubresourceRange range = {};

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);

    explicit operator bool() const { return image && view; }
};

struct FramebufferRenderingConfig {
    vk::RenderingFlags flags = {};
    uint32_t layerCount = 1;
    uint32_t viewMask = 0;

    util::static_vector<bool, 32> enabledColorAttachments = {};
    bool enableDepthAttachment = true;
    bool enableStencilAttachment = true;
    util::static_vector<vk::AttachmentLoadOp, 32> colorLoadOps = {};
    util::static_vector<vk::AttachmentStoreOp, 32> colorStoreOps = {};
    vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eDontCare;
    vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eStore;

    util::static_vector<vk::ClearColorValue, 32> clearColors = {};
    float clearDepth = 0.0f;
    uint32_t clearStencil = 0;

    consteval static util::static_vector<bool, 32> all(const bool enabled) {
        std::array<bool, 32> arr{};
        std::ranges::fill(arr.begin(), arr.end(), enabled);
        return arr;
    }

    consteval static util::static_vector<vk::AttachmentLoadOp, 32> all(const vk::AttachmentLoadOp load_op) {
        std::array<vk::AttachmentLoadOp, 32> arr{};
        std::ranges::fill(arr.begin(), arr.end(), load_op);
        return arr;
    }

    consteval static util::static_vector<vk::AttachmentStoreOp, 32> all(const vk::AttachmentStoreOp store_op) {
        std::array<vk::AttachmentStoreOp, 32> arr{};
        std::ranges::fill(arr.begin(), arr.end(), store_op);
        return arr;
    }
};

class Framebuffer {
    std::array<vk::RenderingAttachmentInfo, 32> colorAttachmentInfos_ = {};
    vk::RenderingAttachmentInfo depthAttachmentInfo_ = {};
    vk::RenderingAttachmentInfo stencilAttachmentInfo_ = {};

public:
    util::static_vector<Attachment, 32> colorAttachments = {};
    Attachment depthAttachment = {};
    Attachment stencilAttachment = {};

    vk::RenderingInfo renderingInfo(const vk::Rect2D &area, const FramebufferRenderingConfig &config = {});

    void barrierColor(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrierColor(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);

    void barrierDepth(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrierDepth(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);

    void barrierStencil(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrierStencil(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);
};
