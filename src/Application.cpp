#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderObject.h"
#include "gltf/Gltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>


#include "Camera.h"
#include "Image.h"
#include "Descriptors.h"
#include "Input.h"

Application::Application() {
    backend = std::make_unique<GraphicsBackend>();
    backend->createCommandBuffers(frameResources.size());
}

Application::~Application() = default;

struct SceneUniforms {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 camera;
};

struct MaterialUniforms {
    alignas(16) glm::vec4 albedoFectors;
    alignas(16) glm::vec4 mrnFactors;
};

void Application::run() {
    auto &device = backend->device;

    Camera camera(90, 0.1, {}, {});
    static Input input(*backend->window);
    glfwSetKeyCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, int key, int scancode, int action, int mods) {
        input.onKey(window, key, scancode, action, mods);
    });
    glfwSetCursorPosCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, double x, double y) {
        input.onCursorPos(window, x, y);
    });
    glfwSetMouseButtonCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, int button, int action, int mods) {
        input.onMouseButton(window, button, action, mods);
    });
    glfwSetScrollCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, double dx, double dy) {
       input.onScroll(window, dx, dy);
    });
    glfwSetCharCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, unsigned int codepoint) {
        input.onChar(window, codepoint);
    });
    glfwSetWindowFocusCallback(static_cast<GLFWwindow *>(backend->window), [](GLFWwindow *window, int focused) {
        input.invalidate();
    });

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

    gltf::SceneData gltf_data = gltf::load("assets/sponza.glb");

    std::vector<Image> textures;
    std::vector<vk::UniqueImageView> texture_views;
    StagingUploader staging_uploader(*backend->allocator);

    Image default_albedo;
    Image default_normal;
    Image default_omr;
    {
        auto cmd_buf = backend->createTransientCommandBuffer();
        std::vector<uint8_t> albedo(16 * 16 * 4);
        std::fill(albedo.begin(), albedo.end(), 0xff);
        default_albedo = Image::create(*backend->allocator, *cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8B8A8Unorm, .width = 16, .height = 16});

        std::vector<uint8_t> normal(16 * 16 * 2);
        std::fill(normal.begin(), normal.end(), 0x7f);
        default_normal = Image::create(*backend->allocator, *cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8Unorm, .width = 16, .height = 16});

        std::vector<uint8_t> omr(16 * 16 * 4);
        std::fill(omr.begin(), omr.end(), 0xff);
        default_omr = Image::create(*backend->allocator, *cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8B8A8Unorm, .width = 16, .height = 16});

        backend->submit(cmd_buf, true);
        staging_uploader.releaseAll();
    }
    auto default_albedo_view = default_albedo.createDefaultView(*backend->device);
    auto default_normal_view = default_normal.createDefaultView(*backend->device);
    auto default_omr_view = default_omr.createDefaultView(*backend->device);

    textures.reserve(gltf_data.images.size());
    texture_views.reserve(gltf_data.images.size());
    for (const auto &image: gltf_data.images) {
        if(image.pixels.empty()) {
            texture_views.emplace_back() = default_albedo.createDefaultView(*backend->device);
            continue;
        }
        auto cmd_buf = backend->createTransientCommandBuffer();
        auto &texture = textures.emplace_back();
        texture = Image::create(*backend->allocator, *cmd_buf, staging_uploader.stage(image.pixels), ImageCreateInfo::from(image));
        texture.generateMipmaps(*cmd_buf);
        texture.barrier(*cmd_buf, ImageResourceAccess::FRAGMENT_SHADER_READ);

        backend->submit(cmd_buf, true);
        staging_uploader.releaseAll();

        texture_views.emplace_back() = texture.createDefaultView(*backend->device);
    }

    std::vector<DescriptorSet> material_descriptors;

    DescriptorSetLayout scene_descriptor_layout(
        *backend->device, {},
        DescriptorBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment) // uniforms
    );
    DescriptorSetLayout material_descriptor_layout(
        *backend->device, {},
        DescriptorBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // albedo
        DescriptorBinding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // normal
        DescriptorBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // omr
        DescriptorBinding(3, vk::DescriptorType::eInlineUniformBlock, vk::ShaderStageFlagBits::eFragment, sizeof(MaterialUniforms)) // mat factors
    );

    DescriptorAllocator descriptor_allocator(*device);
    for (auto &material: gltf_data.materials) {
        material_descriptors.emplace_back() = descriptor_allocator.allocate(material_descriptor_layout);
        const auto &descriptor_set = material_descriptors.back();
        vk::DescriptorImageInfo albedo_image_info = {
            .sampler = *sampler,
            .imageView = material.albedo == -1 ? *default_albedo_view : *texture_views.at(material.albedo),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo normal_image_info = {
            .sampler = *sampler,
            .imageView = material.normal == -1 ? *default_normal_view : *texture_views.at(material.normal),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo orm_image_info = {
            .sampler = *sampler,
            .imageView = material.omr == -1 ? *default_omr_view : *texture_views.at(material.omr),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        MaterialUniforms material_uniforms = {
            .albedoFectors = material.albedoFactor,
            .mrnFactors = glm::vec4(material.metaillicFactor, material.roughnessFactor, material.normalFactor, 0.0),
        };
        vk::WriteDescriptorSetInlineUniformBlock mat_info = {
            .dataSize = sizeof(MaterialUniforms),
            .pData = &material_uniforms
        };
        device->updateDescriptorSets({
                                         descriptor_set.write(0).setPImageInfo(&albedo_image_info),
                                         descriptor_set.write(1).setPImageInfo(&normal_image_info),
                                         descriptor_set.write(2).setPImageInfo(&orm_image_info),
                                         descriptor_set.write(3).setPNext(&mat_info),
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
    auto [tangent_buf, tb_alloc] = backend->allocator->createBufferUnique({.size = gltf_data.vertex_tangent_data.size(), .usage = buffer_usage},
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
    staging_uploader.upload(*staging_cmd_buf, gltf_data.vertex_tangent_data, *tangent_buf);
    staging_uploader.upload(*staging_cmd_buf, gltf_data.vertex_texcoord_data, *texcoord_buf);
    staging_uploader.upload(*staging_cmd_buf, gltf_data.index_data, *index_buf);
    backend->submit(staging_cmd_buf, true);
    staging_uploader.releaseAll();

    auto uniform_buffers = frameResources.create([this] {
        vma::AllocationInfo ub_alloc_info = {};
        auto [uniform_buffer, ub_alloc] = backend->allocator->createBufferUnique(
            {
                .size = sizeof(SceneUniforms),
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
            SceneUniforms *pointer{};
        };
        return Return{
            std::move(uniform_buffer),
            std::move(ub_alloc),
            static_cast<SceneUniforms *>(ub_alloc_info.pMappedData)
        };
    });

    auto scene_descriptor_sets = frameResources.create([&descriptor_allocator, &scene_descriptor_layout] {
        return descriptor_allocator.allocate(scene_descriptor_layout);
    });

    for (int i = 0; i < frameResources.size(); ++i) {
        vk::DescriptorBufferInfo uniform_buffer_info = {
            .buffer = *uniform_buffers.get(i).buffer,
            .offset = 0,
            .range = sizeof(SceneUniforms)
        };
        device->updateDescriptorSets({
                                         scene_descriptor_sets.get(i).write(0).setBufferInfo(uniform_buffer_info)
                                     }, {});
    }

    loader = std::make_unique<ShaderLoader>();
#ifndef NDEBUG
    loader->debug = true;
#endif
    std::array descriptor_set_layouts = {scene_descriptor_layout.get(), material_descriptor_layout.get()};
    vk::PushConstantRange push_constant_range = {.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)};
    std::array push_constant_ranges = {push_constant_range};

    const auto load_shader = [&]() {
        auto vert_sh = loader->load("assets/test.vert");
        auto frag_sh = loader->load("assets/test.frag");
        return Shader(*backend->device, {vert_sh, frag_sh}, descriptor_set_layouts, push_constant_ranges);
    };
    auto shader = load_shader();

    const auto create_semaphore = [device] { return device->createSemaphoreUnique(vk::SemaphoreCreateInfo{}); };
    auto image_available_semaphores = frameResources.create(create_semaphore);
    auto render_finished_semaphores = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [device] {
        return device->createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frameResources.create(create_signaled_fence);

    auto swapchain = Swapchain(*backend->allocator, backend->phyicalDevice, *backend->device, *backend->window, *backend->surface);

    while (!backend->window->shouldClose()) {
        frameResources.advance();
        auto in_flight_fence = in_flight_fences.get();
        while (device->waitForFences({in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        input.update();

        auto image_available_semaphore = image_available_semaphores.get();
        auto render_finished_semaphore = render_finished_semaphores.get();

        if(input.isKeyPress(GLFW_KEY_F5)) {
            Logger::info("Reloading shader");
            try {
                shader = load_shader();
            }catch (const std::exception &exc){
                Logger::error("Reload failed: " + std::string(exc.what()));
            }
        }

        if(input.isMouseReleased() && input.isMousePress(GLFW_MOUSE_BUTTON_LEFT)) {
            input.captureMouse();
        } else if(input.isMouseCaptured() && input.isKeyPress(GLFW_KEY_LEFT_ALT)) {
            input.releaseMouse();
        }

        if(input.isMouseCaptured()) {
            // yaw
            camera.angles.y -= input.mouseDelta().x * glm::radians(0.15f);
            camera.angles.y = glm::wrapAngle(camera.angles.y);

            // pitch
            camera.angles.x -= input.mouseDelta().y * glm::radians(0.15f);
            camera.angles.x = glm::clamp(camera.angles.x, -glm::half_pi<float>(), glm::half_pi<float>());

            glm::vec3 move_input = {
                input.isKeyDown(GLFW_KEY_D) - input.isKeyDown(GLFW_KEY_A),
                input.isKeyDown(GLFW_KEY_SPACE) - input.isKeyDown(GLFW_KEY_LEFT_CONTROL),
                input.isKeyDown(GLFW_KEY_S) - input.isKeyDown(GLFW_KEY_W)};
            glm::vec3 velocity = move_input * 5.0f;
            velocity = glm::mat3(glm::rotate(glm::mat4(1.0), camera.angles.y, {0, 1, 0})) * velocity;
            camera.position += velocity * input.timeDelta();
        }

        camera.setViewport(swapchain.width(), swapchain.height());
        camera.updateViewMatrix();

        SceneUniforms uniforms = {
            .view = camera.viewMatrix(),
            .proj = camera.projectionMatrix(),
            .camera = glm::vec4(camera.position, 1.0)
        };

        std::memcpy(uniform_buffers.get().pointer, &uniforms, sizeof(uniforms));

        if (!swapchain.next(image_available_semaphore)) {
            continue;
        }
        // Reset fence once we are sure that we are submitting work
        device->resetFences({in_flight_fence});

        ImageRef color_attachment(swapchain.colorImage(), swapchain.colorFormat(), {
                                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  });
        ImageRef depth_attachment(swapchain.depthImage(), swapchain.depthFormat(), {
                                      .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  });

        auto &command_buffer = *backend->commandBuffers.at(frameResources.frame());
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        color_attachment.barrier(command_buffer, ImageResourceAccess::COLOR_ATTACHMENT_WRITE);
        depth_attachment.barrier(command_buffer, ImageResourceAccess::DEPTH_ATTACHMENT_READ, ImageResourceAccess::DEPTH_ATTACHMENT_WRITE);

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
            .clearValue = {.depthStencil = {0.0f, 0}}
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
            .vertexBindingDescriptions = gltf::Vertex::bindingDescriptors,
            .vertexAttributeDescriptions = gltf::Vertex::attributeDescriptors,
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
            .cullMode = vk::CullModeFlagBits::eNone,
            .frontFace = vk::FrontFace::eCounterClockwise, // TODO: why CCW?!?
            .depthCompareOp = vk::CompareOp::eGreaterOrEqual
        };
        pipeline_config.apply(command_buffer, shader.stageFlags());

        command_buffer.bindVertexBuffers(0, {*position_buf, *normal_buf, *tangent_buf, *texcoord_buf}, {0, 0, 0, 0});
        command_buffer.bindIndexBuffer(*index_buf, 0, vk::IndexType::eUint32);
        shader.bindDescriptorSet(command_buffer, 0, scene_descriptor_sets.get().get());

        for (const auto &instance: gltf_data.instances) {
            shader.bindDescriptorSet(command_buffer, 1, material_descriptors[instance.material.index].get());

            command_buffer.pushConstants(shader.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation);
            command_buffer.drawIndexed(instance.indexCount, 1, instance.indexOffset, instance.vertexOffset, 0);
        }
        command_buffer.endRendering();

        color_attachment.barrier(command_buffer, ImageResourceAccess::PRESENT_SRC);

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
