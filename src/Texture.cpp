#include "Texture.h"

#include <cmath>
#include <filesystem>
#include <ranges>
#include <stb_image.h>
#include <vulkan/utility/vk_format_utils.h>

#include "GraphicsBackend.h"
#include "Logger.h"


template<int SrcCh, int DstCh>
    requires (SrcCh >= 1) && (SrcCh <= 4) && (DstCh >= 1) && (DstCh <= 4)
static void copy_pixels(const unsigned char *src, unsigned char *dst, size_t elements) {
    for (size_t i = 0; i < elements; i++) {
        // always copy first
        dst[i * DstCh] = src[i * SrcCh];
        if constexpr (DstCh < SrcCh) {
            if constexpr (DstCh > 1) dst[i * DstCh + 1] = src[i * SrcCh + 1];
            if constexpr (DstCh > 2) dst[i * DstCh + 2] = src[i * SrcCh + 2];
            if constexpr (DstCh > 3) dst[i * DstCh + 3] = src[i * SrcCh + 3];
        } else {
            if constexpr (SrcCh > 1) dst[i * DstCh + 1] = src[i * SrcCh + 1];
            if constexpr (SrcCh > 2) dst[i * DstCh + 2] = src[i * SrcCh + 2];
            if constexpr (SrcCh > 3) dst[i * DstCh + 3] = src[i * SrcCh + 3];

            // extend
            if (DstCh > SrcCh) {
                // This might be wrong
                if constexpr (DstCh - SrcCh >= 1) dst[i * DstCh + SrcCh] = 0;
                if constexpr (DstCh - SrcCh >= 2) dst[i * DstCh + SrcCh + 1] = 0;
                if constexpr (DstCh - SrcCh >= 3) dst[i * DstCh + SrcCh + 2] = 0;
                if constexpr (DstCh == 4) dst[i * DstCh + 3] = 255;
            }
        }
    }
}

static void copy_pixels(const unsigned char *src, int src_channels, unsigned char *dst, int dst_chanels, size_t elements) {
    constexpr std::array jmp = {
        &copy_pixels<1, 1>, &copy_pixels<2, 1>, &copy_pixels<3, 1>, &copy_pixels<4, 1>,
        &copy_pixels<1, 2>, &copy_pixels<2, 2>, &copy_pixels<3, 2>, &copy_pixels<4, 2>,
        &copy_pixels<1, 3>, &copy_pixels<2, 3>, &copy_pixels<3, 3>, &copy_pixels<4, 3>,
        &copy_pixels<1, 4>, &copy_pixels<2, 4>, &copy_pixels<3, 4>, &copy_pixels<4, 4>,
    };
    const int index = (src_channels - 1) + 4 * (dst_chanels - 1);
    jmp[index](src, dst, elements);
}

PlainImageData::~PlainImageData() noexcept {
    std::free(std::exchange(data, nullptr));
}

PlainImageData::PlainImageData(PlainImageData &&other) noexcept
    : data(std::exchange(other.data, nullptr)),
      width(std::exchange(other.width, 0)),
      height(std::exchange(other.height, 0)),
      pixels(std::exchange(other.pixels, {})),
      format(std::exchange(other.format, vk::Format::eUndefined)) {
}

PlainImageData &PlainImageData::operator=(PlainImageData &&other) noexcept {
    if (this == &other)
        return *this;

    std::free(data);
    data = std::exchange(other.data, nullptr);
    width = std::exchange(other.width, 0);
    height = std::exchange(other.height, 0);
    pixels = std::exchange(other.pixels, {});
    format = std::exchange(other.format, vk::Format::eUndefined);
    return *this;
}

PlainImageData PlainImageData::create(vk::Format format, int width, int height, int src_channels, const unsigned char *src_data) {
    int dst_channels = static_cast<int>(vkuFormatComponentCount(static_cast<VkFormat>(format)));

    size_t elements = width * height;
    size_t size = elements * dst_channels;
    auto dst_data = static_cast<unsigned char *>(std::malloc(size));
    copy_pixels(src_data, src_channels, dst_data, dst_channels, elements);

    return {
        dst_data,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height),
        std::span(dst_data, width * height * dst_channels),
        format
    };
}

PlainImageData PlainImageData::create(vk::Format format, const std::filesystem::path &path) {
    int result_channels = static_cast<int>(vkuFormatComponentCount(static_cast<VkFormat>(format)));
    int width, height, channels;
    stbi_uc *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, result_channels);

    return {
        pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height),
        std::span(pixels, width * height * result_channels),
        format
    };
}

Texture::Texture(vma::UniqueImage &&image, vma::UniqueAllocation &&allocation, const TextureCreateInfo &create_info)
    : image(std::move(image)), allocation(std::move(allocation)), info(create_info) {
}

Texture Texture::create(const vma::Allocator &allocator, TextureCreateInfo create_info) {
    if (create_info.mip_levels == -1) {
        create_info.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(create_info.width, create_info.height)))) + 1;
    }

    auto [image, allocation] = allocator.createImageUnique(
        {
            .imageType = create_info.type,
            .format = create_info.format,
            .extent = {
                .width = create_info.width, .height = create_info.height, .depth = create_info.depth
            },
            .mipLevels = create_info.mip_levels,
            .arrayLayers = create_info.array_layers,
            .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        }, {
            .usage = vma::MemoryUsage::eAuto,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });

    return {std::move(image), std::move(allocation), create_info};
}

Texture Texture::create(const vma::Allocator &allocator, const vk::CommandBuffer &cmd_buf, vk::Buffer staged_data, const TextureCreateInfo &create_info) {
    Texture texture = create(allocator, create_info);

    texture.toTransferDstLayout(cmd_buf);
    texture.load(cmd_buf, 0, {}, staged_data);

    return texture;
}

void Texture::toTransferDstLayout(const vk::CommandBuffer &cmd_buf) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = layout,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image,
        .subresourceRange = {
            .aspectMask = imageAspectFlags(),
            .levelCount = info.mip_levels,
            .layerCount = info.array_layers,
        },
    };

    cmd_buf.pipelineBarrier2({
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });

    layout = vk::ImageLayout::eTransferDstOptimal;
}

void Texture::toShaderReadOnlyLayout(const vk::CommandBuffer &cmd_buf) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllGraphics,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = layout,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image,
        .subresourceRange = {
            .aspectMask = imageAspectFlags(),
            .levelCount = info.mip_levels,
            .layerCount = info.array_layers,
        },
    };

    cmd_buf.pipelineBarrier2({
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });

    layout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void Texture::load(const vk::CommandBuffer &cmd_buf, uint32_t level, vk::Extent3D region, const vk::Buffer &data) {
    if (region.width == 0) region.width = info.width;
    if (region.height == 0) region.height = info.height;
    if (region.depth == 0) region.depth = info.depth;

    if (layout != vk::ImageLayout::eTransferDstOptimal) {
        Logger::warning("Texture image not in transfer dst optimal layout.");
        toTransferDstLayout(cmd_buf);
    }

    vk::BufferImageCopy image_copy = {
        .imageSubresource = {
            .aspectMask = imageAspectFlags(),
            .mipLevel = level,
            .layerCount = 1,
        },
        .imageExtent = region,
    };
    cmd_buf.copyBufferToImage(data, *image, vk::ImageLayout::eTransferDstOptimal, image_copy);
}

void Texture::generateMipmaps(const vk::CommandBuffer &cmd_buf) {
    if (layout != vk::ImageLayout::eTransferDstOptimal) {
        Logger::warning("Texture image not in transfer dst optimal layout.");
        toTransferDstLayout(cmd_buf);
    }

    vk::ImageMemoryBarrier2 barrier = {
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = info.array_layers
        }
    };

    // TODO:
    // auto format_properties = backend.phyicalDevice.getFormatProperties(format);
    // if (!(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
    //     Logger::panic("image format does not support linear blitting");
    // }

    auto level_width = static_cast<int32_t>(info.width);
    auto level_height = static_cast<int32_t>(info.height);

    // run for images 1..n, the 0th is expected to be loaded
    for (uint32_t lvl = 1; lvl < info.mip_levels; lvl++) {
        int32_t next_level_width = std::max(level_width / 2, 1);
        int32_t next_level_height = std::max(level_height / 2, 1);

        // transition layout of lower mip to src
        if (layout != vk::ImageLayout::eTransferSrcOptimal) {
            barrier.subresourceRange.baseMipLevel = lvl - 1;
            barrier.oldLayout = layout;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;

            cmd_buf.pipelineBarrier2({
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            });
        }

        vk::ImageBlit blit = {
            .srcSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = lvl - 1,
                .baseArrayLayer = 0,
                .layerCount = info.array_layers,
            },
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{level_width, level_height, 1}},
            .dstSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = lvl,
                .baseArrayLayer = 0,
                .layerCount = info.array_layers,
            },
            .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{next_level_width, next_level_height, 1}}
        };

        cmd_buf.blitImage(
            *image, vk::ImageLayout::eTransferSrcOptimal,
            *image, vk::ImageLayout::eTransferDstOptimal,
            blit, vk::Filter::eLinear);

        level_width = next_level_width;
        level_height = next_level_height;
    }

    // final transition, kinda useless, but brings all levels to the same layout
    if (layout != vk::ImageLayout::eTransferSrcOptimal) {
        barrier.subresourceRange.baseMipLevel = info.mip_levels - 1;
        barrier.oldLayout = layout;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;

        cmd_buf.pipelineBarrier2({
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        });
    }

    layout = vk::ImageLayout::eTransferSrcOptimal;
}

vk::UniqueImageView Texture::createDefaultView(const vk::Device &device) {
    return device.createImageViewUnique({
        .image = *image,
        .viewType = static_cast<vk::ImageViewType>(info.type),
        .format = info.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = info.mip_levels,
            .layerCount = info.array_layers
        }
    });
}


vk::ImageAspectFlags Texture::imageAspectFlags() const {
    switch (info.format) {
        case vk::Format::eUndefined:
            Logger::panic("image format undefined");
        case vk::Format::eS8Uint:
            return vk::ImageAspectFlagBits::eStencil;
        case vk::Format::eD16Unorm:
        case vk::Format::eD32Sfloat:
        case vk::Format::eX8D24UnormPack32:
            return vk::ImageAspectFlagBits::eDepth;
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        default:
            if (info.format > vk::Format::eAstc12x12SrgbBlock)
                Logger::panic("unsupported image format");
            return vk::ImageAspectFlagBits::eColor;
    }
}
