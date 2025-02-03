#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderLoader.h"
#include "gltf/Gltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Texture.h"
#include "Descriptors.h"

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

static bool framebufferResized = false;

static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
    framebufferResized = true;
    Logger::warning("Swapchain needs recreation: framebuffer resized");
}

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

    backend->uploadWithStaging(gltf_data.vertex_position_data, *position_buf);
    backend->uploadWithStaging(gltf_data.vertex_normal_data, *normal_buf);
    backend->uploadWithStaging(gltf_data.vertex_texcoord_data, *texcoord_buf);
    backend->uploadWithStaging(gltf_data.index_data, *index_buf);

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
            .imageView = *texture_views.at(0),
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
    vk::PushConstantRange push_constant_range = {.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)};
    auto [pipeline_layout, pipeline] = loader->link(
        *backend->renderPass, {vert_sh, frag_sh},
        gltf::Vertex::bindingDescriptors(), gltf::Vertex::attributeDescriptors(),
        std::array{*uniform_layout, material_descriptor_layout.get()},
        std::array{push_constant_range},
        {});

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
            .model = glm::mat4(1.0), // deprecated
            .view = glm::lookAt(glm::vec3(glm::cos(time * 0.3) * 8, 6.0f, glm::sin(time * 0.3) * 8), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            .proj = glm::perspective(glm::radians(70.0f), aspect_ratio, 0.1f, 1000.0f)
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

        command_buffer.bindVertexBuffers(0, {*position_buf, *normal_buf, *texcoord_buf}, {0, 0, 0});
        command_buffer.bindIndexBuffer(*index_buf, 0, vk::IndexType::eUint32);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, descriptor_sets.get(), {});

        for (const auto &instance: gltf_data.instances) {
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 1, material_descriptors[instance.material.index].get(), {});

            command_buffer.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation);
            command_buffer.drawIndexed(instance.indexCount, 1, instance.indexOffset, instance.vertexOffset, 0);
        }
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
