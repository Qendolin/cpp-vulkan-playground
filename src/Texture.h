#pragma once

#include <filesystem>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>

class StagingLeaseManager;
class StagedUploadManager;
class StagingBufferAllocator;
class GraphicsBackend;


struct PlainImageData {
    std::unique_ptr<unsigned char[], decltype(&std::free)> data = std::unique_ptr<unsigned char[], decltype(&std::free)>(nullptr, std::free);
    std::span<unsigned char> pixels;
    uint32_t width, height;
    vk::Format format;

    static PlainImageData create(vk::Format format, const std::filesystem::path& path);
    static PlainImageData create(vk::Format format, int width, int height, int channels, const unsigned char* data);

    explicit operator bool() const {
        return static_cast<bool>(data);
    }
};

struct TextureCreateInfo {
    vk::Format format = vk::Format::eUndefined;
    vk::ImageType type = vk::ImageType::e2D;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mip_levels = -1u;
    uint32_t array_layers = 1;

    TextureCreateInfo() = default;

    explicit TextureCreateInfo(const PlainImageData & plain_image_data) {
        width = plain_image_data.width;
        height = plain_image_data.height;
        format = plain_image_data.format;
    }
};

class Texture {
public:
    Texture() = default;

    Texture(vma::UniqueImage&& image, vma::UniqueAllocation&& allocation, const TextureCreateInfo& create_info);

    Texture(const Texture &other) = delete;

    Texture(Texture &&other) noexcept
        : image(std::move(other.image)),
          allocation(std::move(other.allocation)),
          info(other.info),
          layout(other.layout) {
    }

    Texture & operator=(const Texture &other) = delete;

    Texture & operator=(Texture &&other) noexcept {
        image = std::move(other.image);
        allocation = std::move(other.allocation);
        info = other.info;
        layout = other.layout;
        return *this;
    }

    static Texture create(const vma::Allocator& allocator, TextureCreateInfo create_info);
    static Texture create(const vma::Allocator &allocator, const vk::CommandBuffer &cmd_buf, vk::Buffer staged_data, const TextureCreateInfo& create_info);

    void toTransferDstLayout(const vk::CommandBuffer& cmd_buf);

    void toShaderReadOnlyLayout(const vk::CommandBuffer& cmd_buf);

    void load(const vk::CommandBuffer& cmd_buf, uint32_t level, vk::Extent3D region, const vk::Buffer& data);

    void generateMipmaps(const vk::CommandBuffer& cmd_buf);

    vk::UniqueImageView createDefaultView(const vk::Device& device);

private:
    vma::UniqueImage image;
    vma::UniqueAllocation allocation;
    TextureCreateInfo info;
    // layout of the entire image, otherwise undefined
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;

    [[nodiscard]] vk::ImageAspectFlags imageAspectFlags() const;
};
