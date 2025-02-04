#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderObject.h"
#include "gltf/Gltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Texture.h"
#include "Descriptors.h"

Application::Application() {
    backend = std::make_unique<GraphicsBackend>();
    backend->createCommandBuffers(frameResources.size());
}

Application::~Application() = default;

struct Uniforms {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

// static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
//     framebufferResized = true;
//     Logger::warning("Swapchain needs recreation: framebuffer resized");
// }

using MaterialDescriptorLayout = DescriptorSetLayout<
    DescriptorBinding<0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment>, // albedo
    DescriptorBinding<1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment>, // normal
    DescriptorBinding<2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment> // orm
>;

void Application::run() {
    auto &device = backend->device;

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

    gltf::SceneData gltf_data = gltf::load("assets/gltf_test.glb");

    std::vector<Texture> textures;
    std::vector<vk::UniqueImageView> texture_views;

    textures.reserve(gltf_data.images.size());
    texture_views.reserve(gltf_data.images.size());
    StagingUploader staging_uploader(*backend->allocator);
    for (const auto &image: gltf_data.images) {
        auto cmd_buf = backend->createTransientCommandBuffer();
        auto &texture = textures.emplace_back();
        texture = Texture::create(*backend->allocator, *cmd_buf, staging_uploader.stage(image.pixels), TextureCreateInfo(image));
        texture.generateMipmaps(*cmd_buf);
        texture.toShaderReadOnlyLayout(*cmd_buf);

        backend->submit(cmd_buf, true);
        staging_uploader.releaseAll();

        texture_views.emplace_back() = texture.createDefaultView(*backend->device);
    }

    std::vector<DescriptorSet> material_descriptors;

    DescriptorAllocator descriptor_allocator(*device);
    DescriptorSetLayout material_descriptor_layout = MaterialDescriptorLayout(*device, {});
    for (auto &material: gltf_data.materials) {
        material_descriptors.emplace_back() = descriptor_allocator.allocate(material_descriptor_layout);
        const auto &descriptor_set = material_descriptors.back();
        vk::DescriptorImageInfo albedo_image_info = {
            .sampler = *sampler,
            .imageView = *texture_views.at(material.albedo),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo normal_image_info = {
            .sampler = *sampler,
            .imageView = *texture_views.at(material.normal),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo orm_image_info = {
            .sampler = *sampler,
            .imageView = *texture_views.at(material.orm),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        device->updateDescriptorSets({
                                         descriptor_set.write(0).setPImageInfo(&albedo_image_info),
                                         descriptor_set.write(1).setPImageInfo(&normal_image_info),
                                         descriptor_set.write(2).setPImageInfo(&orm_image_info),
                                     }, {});
    }

    vma::AllocationCreateInfo allocation_create_info = {
        .usage = vma::MemoryUsage::eAutoPreferDevice,
        .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
    };
    auto buffer_usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;

    auto [position_buf, pb_alloc] = backend->allocator->createBufferUnique({.size = gltf_data.vertex_position_data.size(), .usage = buffer_usage},
                                                                           allocation_create_info);
    auto [normal_buf, nb_alloc] = backend->allocator->createBufferUnique({.size = gltf_data.vertex_normal_data.size(), .usage = buffer_usage},
                                                                         allocation_create_info);
    auto [texcoord_buf, tcb_alloc] = backend->allocator->createBufferUnique({.size = gltf_data.vertex_texcoord_data.size(), .usage = buffer_usage},
                                                                            allocation_create_info);
    auto [index_buf, ib_alloc] = backend->allocator->createBufferUnique({
                                                                            .size = gltf_data.index_data.size(),
                                                                            .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                                                                                     vk::BufferUsageFlagBits::eTransferDst
                                                                        }, allocation_create_info);
    auto staging_cmd_buf = backend->createTransientCommandBuffer();
    staging_uploader.upload(*staging_cmd_buf, gltf_data.vertex_position_data, *position_buf);
    staging_uploader.upload(*staging_cmd_buf, gltf_data.vertex_normal_data, *normal_buf);
    staging_uploader.upload(*staging_cmd_buf, gltf_data.vertex_texcoord_data, *texcoord_buf);
    staging_uploader.upload(*staging_cmd_buf, gltf_data.index_data, *index_buf);
    backend->submit(staging_cmd_buf, true);
    staging_uploader.releaseAll();

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
    vk::UniqueDescriptorSetLayout uniform_layout = backend->device->createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo().setBindings(uniform_binding)
    );

    std::initializer_list<vk::DescriptorPoolSize> uniform_pool_sizes = {
        {
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = static_cast<uint32_t>(frameResources.size())
        },
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
        auto descriptor_write = {
            vk::WriteDescriptorSet{
                .dstSet = descriptor_sets.get(i),
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &uniform_buffer_info,
            },
        };
        device->updateDescriptorSets(descriptor_write, nullptr);
    }

    loader = std::make_unique<ShaderLoader2>();
#ifndef NDEBUG
    loader->debug = true;
#endif
    auto vert_sh = loader->load("assets/test.vert");
    auto frag_sh = loader->load("assets/test.frag");

    std::array descriptor_set_layouts = {*uniform_layout, material_descriptor_layout.get()};
    vk::PushConstantRange push_constant_range = {.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)};
    std::array push_constant_ranges = {push_constant_range};

    auto shader = Shader(*backend->device, {vert_sh, frag_sh}, descriptor_set_layouts, push_constant_ranges);

    const auto create_semaphore = [device] { return device->createSemaphoreUnique(vk::SemaphoreCreateInfo{}); };
    auto image_available_semaphores = frameResources.create(create_semaphore);
    auto render_finished_semaphores = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [device] {
        return device->createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frameResources.create(create_signaled_fence);

    auto swapchain = Swapchain(*backend->allocator, backend->phyicalDevice, *backend->device, *backend->window, *backend->surface);

    // glfwSetFramebufferSizeCallback(*backend->window, framebufferResizeCallback);

    while (!backend->window->shouldClose()) {
        frameResources.advance();
        auto in_flight_fence = in_flight_fences.get();
        while (device->waitForFences({in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        glfwPollEvents();

        auto image_available_semaphore = image_available_semaphores.get();
        auto render_finished_semaphore = render_finished_semaphores.get();

        float time = static_cast<float>(glfwGetTime());
        float aspect_ratio = swapchain.width() / swapchain.height();
        Uniforms uniforms = {
            .model = glm::mat4(1.0), // deprecated
            .view = glm::lookAt(glm::vec3(glm::cos(time * 0.3) * 8, 6.0f, glm::sin(time * 0.3) * 8), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            .proj = glm::perspective(glm::radians(70.0f), aspect_ratio, 0.1f, 1000.0f)
        };

        std::memcpy(uniform_buffers.get().pointer, &uniforms, sizeof(uniforms));

        if (!swapchain.next(image_available_semaphore)) {
            continue;
        }

        // Reset fence once we are sure that we are submitting work
        device->resetFences({in_flight_fence});

        auto &command_buffer = *backend->commandBuffers.at(frameResources.frame());
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{});

        vk::ImageMemoryBarrier2 color_attachment_attachment_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eAttachmentOptimal,
            .image = swapchain.colorImage(),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            }
        };
        vk::ImageMemoryBarrier2 depth_attachment_attachment_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eAttachmentOptimal,
            .image = swapchain.depthImage(),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 1,
            }
        };

        auto attachment_barriers = std::array{color_attachment_attachment_barrier, depth_attachment_attachment_barrier};
        command_buffer.pipelineBarrier2({
            .imageMemoryBarrierCount = attachment_barriers.size(),
            .pImageMemoryBarriers = attachment_barriers.data()
        });

        vk::RenderingAttachmentInfoKHR color_attachment_info{
            .imageView = swapchain.colorView(),
            .imageLayout = vk::ImageLayout::eAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .clearValue = {.color = {.float32 = std::array{0.0f, 0.0f, 0.0f, 1.0f}}}
        };
        vk::RenderingAttachmentInfoKHR depth_attachment_info{
            .imageView = swapchain.depthView(),
            .imageLayout = vk::ImageLayout::eAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .clearValue = {.depthStencil = {1.0f, 0}}
        };
        vk::RenderingInfoKHR redering_info = {
            .renderArea = swapchain.area(),
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment_info,
            .pDepthAttachment = &depth_attachment_info
        };
        command_buffer.beginRenderingKHR(redering_info);
        command_buffer.bindShadersEXT(shader.stages(), shader.shaders());

        PipelineConfig pipeline_config = {
            .vertexBindingDescriptions = gltf::Vertex::bindingDescriptors(),
            .vertexAttributeDescriptions = gltf::Vertex::attributeDescriptors(),
            .viewports = {
                {
                    vk::Viewport{
                        0.0f, swapchain.height(),
                        swapchain.width(), -swapchain.height(),
                        0.0f, 1.0f
                    }
                }
            },
            .scissors = {{swapchain.area()}},
        };
        pipeline_config.apply(command_buffer, shader.stageFlags());

        command_buffer.bindVertexBuffers(0, {*position_buf, *normal_buf, *texcoord_buf}, {0, 0, 0});
        command_buffer.bindIndexBuffer(*index_buf, 0, vk::IndexType::eUint32);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shader.pipelineLayout(), 0, descriptor_sets.get(), {});

        for (const auto &instance: gltf_data.instances) {
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shader.pipelineLayout(), 1, material_descriptors[instance.material.index].get(),
                                              {});

            command_buffer.pushConstants(shader.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation);
            command_buffer.drawIndexed(instance.indexCount, 1, instance.indexOffset, instance.vertexOffset, 0);
        }
        command_buffer.endRendering();

        vk::ImageMemoryBarrier2 color_attachment_present_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
            .oldLayout = vk::ImageLayout::eAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .image = swapchain.colorImage(),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .levelCount = 1,
                .layerCount = 1,
            }
        };

        command_buffer.pipelineBarrier2({
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &color_attachment_present_barrier
        });

        command_buffer.end();

        vk::PipelineStageFlags pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit_info = vk::SubmitInfo()
                .setCommandBuffers(command_buffer)
                .setWaitSemaphores(image_available_semaphore)
                .setWaitDstStageMask(pipe_stage_flags)
                .setSignalSemaphores(render_finished_semaphore);
        backend->graphicsQueue.submit({submit_info}, in_flight_fence);

        swapchain.present(backend->graphicsQueue, vk::PresentInfoKHR().setWaitSemaphores(render_finished_semaphore));
    }

    Logger::info("Exited main loop");
    device->waitIdle();
}
