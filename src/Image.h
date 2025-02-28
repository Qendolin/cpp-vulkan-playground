#pragma once

#include <filesystem>
#include <span>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan.hpp>

class PlainImageData {
    unsigned char *data;
    bool owning = false;

public:
    uint32_t width;
    uint32_t height;
    std::span<unsigned char> pixels;
    vk::Format format;

    PlainImageData() noexcept : data(nullptr), width(0), height(0), pixels({}), format(vk::Format::eUndefined) {}

    PlainImageData(std::span<unsigned char> pixels, uint32_t width, uint32_t height, vk::Format format) noexcept;

    PlainImageData(std::unique_ptr<unsigned char> data, size_t size, uint32_t width, uint32_t height, vk::Format format) noexcept;

    ~PlainImageData() noexcept;

    PlainImageData(PlainImageData &&other) noexcept;

    PlainImageData &operator=(PlainImageData &&other) noexcept;

    PlainImageData(const PlainImageData &other) = delete;

    PlainImageData &operator=(const PlainImageData &other) = delete;

    explicit operator bool() const { return static_cast<bool>(data); }

    void copyChannels(PlainImageData &dst, std::initializer_list<int> mapping) const;

    void fill(std::initializer_list<int> channels, std::initializer_list<unsigned char> values);

    static PlainImageData create(vk::Format format, const std::filesystem::path &path);

    static PlainImageData create(vk::Format format, int width, int height, int channels = 0, const unsigned char *data = nullptr);
};


struct ImageCreateInfo {
    vk::Format format = vk::Format::eUndefined;
    vk::ImageType type = vk::ImageType::e2D;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mip_levels = -1u;
    uint32_t array_layers = 1;

    static constexpr ImageCreateInfo from(const PlainImageData &plain_image_data) {
        return {
            .format = plain_image_data.format,
            .width = plain_image_data.width,
            .height = plain_image_data.height,
        };
    }
};

struct ImageResourceAccess {
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eTopOfPipe;
    vk::AccessFlags2 access = vk::AccessFlagBits2::eNone;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;

    static const ImageResourceAccess TRANSFER_WRITE;
    static const ImageResourceAccess FRAGMENT_SHADER_READ;
    static const ImageResourceAccess COLOR_ATTACHMENT_WRITE;
    static const ImageResourceAccess DEPTH_ATTACHMENT_WRITE;
    static const ImageResourceAccess DEPTH_ATTACHMENT_READ;
    static const ImageResourceAccess PRESENT_SRC;
};

class ImageResource {
protected:
    ImageResourceAccess prevAccess = {};

    void barrier(
            vk::Image image,
            vk::ImageSubresourceRange range,
            const vk::CommandBuffer &cmd_buf,
            const ImageResourceAccess &begin,
            const ImageResourceAccess &end
    );
};


class Image : ImageResource {
    [[nodiscard]] vk::Image getImage() const { return *image; }

    [[nodiscard]] vk::ImageSubresourceRange getResourceRange() const {
        return {
            .aspectMask = imageAspectFlags(),
            .levelCount = info.mip_levels,
            .layerCount = info.array_layers,
        };
    }

public:
    Image() = default;

    Image(vma::UniqueImage &&image, vma::UniqueAllocation &&allocation, const ImageCreateInfo &create_info);

    Image(const Image &other) = delete;

    Image(Image &&other) noexcept;

    static Image create(const vma::Allocator &allocator, ImageCreateInfo create_info);

    Image &operator=(const Image &other) = delete;

    Image &operator=(Image &&other) noexcept;

    void load(const vk::CommandBuffer &cmd_buf, uint32_t level, vk::Extent3D region, const vk::Buffer &data);

    void generateMipmaps(const vk::CommandBuffer &cmd_buf);

    vk::UniqueImageView createDefaultView(const vk::Device &device);

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);

private:
    vma::UniqueImage image;
    vma::UniqueAllocation allocation;
    ImageCreateInfo info;

    [[nodiscard]] vk::ImageAspectFlags imageAspectFlags() const;
};

class ImageRef : public ImageResource {
public:
    vk::Image image;
    vk::Format format;
    vk::ImageSubresourceRange range;

    ImageRef(vk::Image image, vk::Format format, vk::ImageSubresourceRange range);

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &begin, const ImageResourceAccess &end);

    void barrier(const vk::CommandBuffer &cmd_buf, const ImageResourceAccess &single);
};
