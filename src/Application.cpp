#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderLoader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Application::Application() {
    backend = std::make_unique<GraphicsBackend>();
    backend->createSwapchain();
    backend->createRenderPass();
    backend->createCommandBuffers(frameResources.size());
}

Application::~Application() = default;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static constexpr auto bindingDescriptors() {
        constexpr std::array desc{
            vk::VertexInputBindingDescription{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = vk::VertexInputRate::eVertex
            }
        };
        return desc;
    }

    static constexpr auto attributeDescriptors() {
        constexpr std::array desc{
            vk::VertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, pos)
            },
            vk::VertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color)
            },
            vk::VertexInputAttributeDescription{
                .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, texCoord)
            }
        };
        return desc;
    }
};

struct Uniforms {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

void transitionImageLayout(GraphicsBackend &backend, vk::Image &image, vk::Format format, uint32_t level_count, vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout) {
    backend.submitImmediate([&image, oldLayout, newLayout, level_count](vk::CommandBuffer cmd_buf) {
        vk::ImageMemoryBarrier2 barrier = {
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = level_count,
                .layerCount = 1,
            },
        };

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        } else {
            throw std::invalid_argument("unsupported layout transition");
        }

        cmd_buf.pipelineBarrier2({
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        });
    });
}

void generateMipmaps(GraphicsBackend &backend, vk::Image& image, vk::Format format, uint32_t width, uint32_t height, uint32_t levels) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    auto format_properties = backend.phyicalDevice.getFormatProperties(format);
    if(!(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        Logger::panic("image format does not support linear blitting");
    }

    backend.submitImmediate([&barrier, image, width, height, levels](vk::CommandBuffer cmd_buf) {
        auto level_width = static_cast<int32_t>(width);
        auto level_height = static_cast<int32_t>(height);

        for (uint32_t i = 1; i < levels; i++) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;

            cmd_buf.pipelineBarrier2({
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            });

            vk::ImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = i-1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffsets = std::array{vk::Offset3D{0,0,0}, vk::Offset3D{level_width, level_height, 1}},
                .dstSubresource = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = i,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffsets = std::array{vk::Offset3D{0,0,0}, vk::Offset3D{std::max(level_width/2, 1), std::max(level_height/2, 1), 1}}
            };

            cmd_buf.blitImage(
                image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal,
                blit, vk::Filter::eLinear);

            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;

            cmd_buf.pipelineBarrier2({
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            });

            level_width = std::max(level_width/2, 1);
            level_height = std::max(level_height/2, 1);
        }

        barrier.subresourceRange.baseMipLevel = levels - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;

        cmd_buf.pipelineBarrier2({
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        });
    });

}

auto loadTexture(GraphicsBackend &backend, std::string_view filename, bool gen_mipmaps) {
    int tex_width, tex_height, tex_channels;
    stbi_uc *pixels = stbi_load(filename.data(), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    uint32_t width = tex_width, height = tex_height;
    backend.copyToStaging(std::span(pixels, width * height * 4)); // stb converts to 4ch
    stbi_image_free(pixels);

    uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    auto [image, image_mem] = backend.allocator->createImageUnique(
        {
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Srgb,
            .extent = {
                .width = width, .height = height, .depth = 1
            },
            .mipLevels = mip_levels,
            .arrayLayers = 1,
            .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        }, {
            .usage = vma::MemoryUsage::eAuto,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });
    transitionImageLayout(backend, *image, vk::Format::eR8G8B8A8Srgb, mip_levels, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal);
    backend.submitImmediate([&backend, &image, width, height](vk::CommandBuffer cmd_buf) {
        vk::BufferImageCopy img_copy_region = {
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .layerCount = 1
            },
            .imageExtent = {.width = width, .height = height, .depth = 1}
        };
        cmd_buf.copyBufferToImage(*backend.stagingBuffer, *image, vk::ImageLayout::eTransferDstOptimal, img_copy_region);
    });

    if(gen_mipmaps) {
        generateMipmaps(backend, *image, vk::Format::eR8G8B8A8Srgb, width, height, mip_levels);
    } else {
        transitionImageLayout(backend, *image, vk::Format::eR8G8B8A8Srgb, mip_levels, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    }


    vk::UniqueImageView image_view = backend.device->createImageViewUnique({
        .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Srgb,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = mip_levels,
            .layerCount = 1
        }
    });
    return std::tuple{std::move(image), std::move(image_mem), std::move(image_view)};
}

static bool framebufferResized = false;

static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
    framebufferResized = true;
    Logger::warning("Swapchain needs recreation: framebuffer resized");
}

void Application::run() {
    auto &device = backend->device;

    auto [image, image_mem, image_view] = loadTexture(*backend, "assets/viking_room.png", true);

    float max_anisotropy = backend->phyicalDevice.getProperties().limits.maxSamplerAnisotropy;
    vk::UniqueSampler sampler = backend->device->createSamplerUnique({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .anisotropyEnable = true,
        .maxAnisotropy = max_anisotropy,
        .maxLod = vk::LodClampNone,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack
    });

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "assets/viking_room.obj")) {
            throw std::runtime_error(warn + err);
        }
        for (const auto &shape: shapes) {
            for (const auto &index: shape.mesh.indices) {
                Vertex vertex{};

                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
                vertex.color = {1.0f, 1.0f, 1.0f};

                vertices.push_back(vertex);
                indices.push_back(indices.size());
            }
        }
    }

    vk::DeviceSize vb_size = vertices.size() * sizeof(vertices[0]);
    auto [vertex_buf, vb_alloc] = backend->allocator->createBufferUnique(
        {
            .size = vb_size,
            .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        }, {
            .usage = vma::MemoryUsage::eAutoPreferDevice,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });
    vk::DeviceSize ib_size = indices.size() * sizeof(indices[0]);
    auto [index_buf, ib_alloc] = backend->allocator->createBufferUnique(
        {
            .size = ib_size,
            .usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        }, {
            .usage = vma::MemoryUsage::eAutoPreferDevice,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });

    backend->uploadWithStaging(vertices, *vertex_buf);
    backend->uploadWithStaging(indices, *index_buf);

    auto uniform_buffers = frameResources.create([this] {
        vma::AllocationInfo ub_alloc_info = {};
        auto [uniform_buffer, ub_alloc] = backend->allocator->createBufferUnique(
            {
                .size = sizeof(Uniforms),
                .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
            }, {
                .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                         vma::AllocationCreateFlagBits::eMapped,
                .usage = vma::MemoryUsage::eAuto,
                .requiredFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                .preferredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
            }, &ub_alloc_info);
        struct Return {
            vma::UniqueBuffer buffer;
            vma::UniqueAllocation alloc;
            Uniforms *pointer{};
        };
        return Return{
            std::move(uniform_buffer),
            std::move(ub_alloc),
            static_cast<Uniforms *>(ub_alloc_info.pMappedData)
        };
    });

    vk::DescriptorSetLayoutBinding uniform_binding = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    };
    vk::DescriptorSetLayoutBinding sampler_binding = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    };

    auto bindings = {uniform_binding, sampler_binding};
    vk::UniqueDescriptorSetLayout uniform_layout = backend->device->createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo().setBindings(bindings)
    );

    std::initializer_list<vk::DescriptorPoolSize> uniform_pool_sizes = {
        {
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = static_cast<uint32_t>(frameResources.size())
        },
        {
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = static_cast<uint32_t>(frameResources.size())
        }
    };

    vk::UniqueDescriptorPool descriptor_pool = device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{
        .maxSets = static_cast<uint32_t>(frameResources.size()),
    }.setPoolSizes(uniform_pool_sizes));
    std::vector layouts(frameResources.size(), *uniform_layout);
    auto descriptor_sets = frameResources.create([&device, &descriptor_pool, &uniform_layout] {
        // Not unique: cannot free
        return device->allocateDescriptorSets({
            .descriptorPool = *descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*uniform_layout
        })[0];
    });

    for (int i = 0; i < frameResources.size(); ++i) {
        vk::DescriptorBufferInfo uniform_buffer_info = {
            .buffer = *uniform_buffers.get(i).buffer,
            .offset = 0,
            .range = sizeof(Uniforms)
        };
        vk::DescriptorImageInfo image_sampler_info = {
            .sampler = *sampler,
            .imageView = *image_view,
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        auto descriptor_write = {
            vk::WriteDescriptorSet{
                .dstSet = descriptor_sets.get(i),
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uniform_buffer_info,
            },
            vk::WriteDescriptorSet{
                .dstSet = descriptor_sets.get(i),
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &image_sampler_info,
            }
        };
        device->updateDescriptorSets(descriptor_write, nullptr);
    }

    loader = std::make_unique<ShaderLoader>(backend->device);
#ifndef NDEBUG
    loader->debug = true;
#endif
    auto vert_sh = loader->load("assets/test.vert");
    auto frag_sh = loader->load("assets/test.frag");
    auto [pipeline_layout, pipeline] = loader->link(
        *backend->renderPass, {vert_sh, frag_sh},
        Vertex::bindingDescriptors(), Vertex::attributeDescriptors(),
        std::array{*uniform_layout}, {});

    const auto create_semaphore = [device] { return device->createSemaphoreUnique(vk::SemaphoreCreateInfo{}); };
    auto image_available_semaphores = frameResources.create(create_semaphore);
    auto render_finished_semaphores = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [device] {
        return device->createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frameResources.create(create_signaled_fence);

    glfwSetFramebufferSizeCallback(*backend->window, framebufferResizeCallback);

    while (!backend->window->shouldClose()) {
        frameResources.advance();
        auto in_flight_fence = in_flight_fences.get();
        while (device->waitForFences({in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        glfwPollEvents();

        auto image_available_semaphore = image_available_semaphores.get();
        auto render_finished_semaphore = render_finished_semaphores.get();

        float time = static_cast<float>(glfwGetTime());
        float aspect_ratio = static_cast<float>(backend->surfaceExtents.width) / static_cast<float>(backend->surfaceExtents.height);
        Uniforms uniforms = {
            .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .proj = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 10.0f)
        };

        std::memcpy(uniform_buffers.get().pointer, &uniforms, sizeof(uniforms));

        uint32_t image_index = 0;
        bool recreate_swapchain = false;
        try {
            auto image_acquistion_result = device->acquireNextImageKHR(*backend->swapchain, UINT64_MAX,
                                                                       image_available_semaphore, nullptr);
            if (image_acquistion_result.result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
                recreate_swapchain = true;
            }
            image_index = image_acquistion_result.value;
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            recreate_swapchain = true;
        }

        if (recreate_swapchain) {
            backend->recreateSwapchain();
            continue;
        }

        // Reset fence once we are sure that we are submitting work
        device->resetFences({in_flight_fence});

        auto &command_buffer = *backend->commandBuffers.at(frameResources.frame());
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{});

        auto clear_vales = {
            vk::ClearValue{.color = {{{0.0f, 0.0f, 0.0f, 1.0f}}}},
            vk::ClearValue{.depthStencil = {1.0f, 0}}
        };
        vk::RenderPassBeginInfo render_pass_begin_info = {
            .renderPass = *backend->renderPass,
            .framebuffer = *backend->framebuffers[image_index],
            .renderArea = {
                .offset = {},
                .extent = backend->surfaceExtents
            },
        };
        render_pass_begin_info.setClearValues(clear_vales);
        command_buffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

        // https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
        command_buffer.setViewport(0, {
                                       {
                                           0.0f, static_cast<float>(backend->surfaceExtents.height),
                                           static_cast<float>(backend->surfaceExtents.width),
                                           -static_cast<float>(backend->surfaceExtents.height),
                                           0.0f, 1.0f
                                       }
                                   });

        command_buffer.setScissor(0, {{{}, backend->surfaceExtents}});
        command_buffer.setCullMode(vk::CullModeFlagBits::eBack);
        command_buffer.setDepthTestEnable(true);
        command_buffer.setDepthWriteEnable(true);
        command_buffer.setDepthCompareOp(vk::CompareOp::eLess);
        command_buffer.setStencilTestEnable(false);
        command_buffer.setStencilOp(vk::StencilFaceFlagBits::eFrontAndBack, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                    vk::StencilOp::eKeep, vk::CompareOp::eNever);

        command_buffer.bindVertexBuffers(0, {*vertex_buf}, {0});
        command_buffer.bindIndexBuffer(*index_buf, 0, vk::IndexType::eUint32);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0,
                                          descriptor_sets.get(), {});

        command_buffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        command_buffer.endRenderPass();
        command_buffer.end();

        vk::PipelineStageFlags constexpr pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit_info = vk::SubmitInfo()
                .setCommandBuffers(command_buffer)
                .setWaitSemaphores(image_available_semaphore)
                .setWaitDstStageMask(pipe_stage_flags)
                .setSignalSemaphores(render_finished_semaphore);
        backend->graphicsQueue.submit({submit_info}, in_flight_fence);

        vk::PresentInfoKHR present_info = vk::PresentInfoKHR()
                .setWaitSemaphores(render_finished_semaphore)
                .setSwapchains(*backend->swapchain)
                .setImageIndices(image_index);

        recreate_swapchain = false;
        try {
            vk::Result result = backend->graphicsQueue.presentKHR(present_info);
            if (result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
                recreate_swapchain = true;
            }
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            recreate_swapchain = true;
        }

        if (recreate_swapchain || framebufferResized) {
            framebufferResized = false;
            backend->recreateSwapchain();
        }
    }

    Logger::info("Exited main loop");
    device->waitIdle();
}
