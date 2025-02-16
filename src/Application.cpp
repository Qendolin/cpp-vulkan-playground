#include "Application.h"

#include "GraphicsBackend.h"
#include "Swapchain.h"
#include "Logger.h"
#include "ShaderObject.h"
#include "gltf/Gltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>


#include "Camera.h"
#include "CommandPool.h"
#include "Image.h"
#include "Descriptors.h"
#include "FrameResource.h"
#include "debug/Performance.h"
#include "glfw/Input.h"
#include "imgui/ImGui.h"

Application::Application() {
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
    auto ctx = AppContext({.width = 1600, .height = 900, .title = "Vulkan Playground"});
    auto frameResources = FrameResourceManager(ctx.swapchain->imageCount());

    const auto device = ctx.device.get();
    const auto phyical_device = ctx.device.physicalDevice;
    auto &allocator = *ctx.device.allocator;
    auto &input = *ctx.window.input;
    auto &swapchain = *ctx.swapchain;
    auto transfer_commands = CommandPool(device, ctx.device.transferQueue, ctx.device.transferQueueFamily, CommandPool::UseMode::Single);
    auto one_time_commands = CommandPool(device, ctx.device.mainQueue, ctx.device.mainQueueFamily, CommandPool::UseMode::Single);

    float max_anisotropy = phyical_device.getProperties().limits.maxSamplerAnisotropy;
    vk::UniqueSampler sampler = device.createSamplerUnique({
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
    StagingUploader staging_uploader(allocator);

    Image default_albedo;
    Image default_normal;
    Image default_omr;
    {
        auto cmd_buf = one_time_commands.create();
        std::vector<uint8_t> albedo(16 * 16 * 4);
        std::fill(albedo.begin(), albedo.end(), 0xff);
        default_albedo = Image::create(allocator, cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8B8A8Unorm, .width = 16, .height = 16});
        default_albedo.generateMipmaps(cmd_buf);

        std::vector<uint8_t> normal(16 * 16 * 2);
        std::fill(normal.begin(), normal.end(), 0x7f);
        default_normal = Image::create(allocator, cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8Unorm, .width = 16, .height = 16});
        default_normal.generateMipmaps(cmd_buf);

        std::vector<uint8_t> omr(16 * 16 * 4);
        std::fill(omr.begin(), omr.end(), 0xff);
        default_omr = Image::create(allocator, cmd_buf, staging_uploader.stage(albedo), {.format = vk::Format::eR8G8B8A8Unorm, .width = 16, .height = 16});
        default_omr.generateMipmaps(cmd_buf);

        one_time_commands.submitAndWait(cmd_buf);
        staging_uploader.releaseAll();
    }
    auto default_albedo_view = default_albedo.createDefaultView(device);
    auto default_normal_view = default_normal.createDefaultView(device);
    auto default_omr_view = default_omr.createDefaultView(device);

    textures.reserve(gltf_data.images.size());
    texture_views.reserve(gltf_data.images.size());
    for (const auto &image: gltf_data.images) {
        if (image.pixels.empty()) {
            texture_views.emplace_back() = default_albedo.createDefaultView(device);
            continue;
        }
        auto cmd_buf = one_time_commands.create();
        auto &texture = textures.emplace_back();
        texture = Image::create(allocator, cmd_buf, staging_uploader.stage(image.pixels), ImageCreateInfo::from(image));
        texture.generateMipmaps(cmd_buf);
        texture.barrier(cmd_buf, ImageResourceAccess::FRAGMENT_SHADER_READ);

        one_time_commands.submitAndWait(cmd_buf);
        staging_uploader.releaseAll();

        texture_views.emplace_back() = texture.createDefaultView(device);
    }

    std::vector<DescriptorSet> material_descriptors;

    DescriptorSetLayout scene_descriptor_layout(
        device, {},
        DescriptorBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment) // uniforms
    );
    DescriptorSetLayout material_descriptor_layout(
        device, {},
        DescriptorBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // albedo
        DescriptorBinding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // normal
        DescriptorBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // omr
        DescriptorBinding(3, vk::DescriptorType::eInlineUniformBlock, vk::ShaderStageFlagBits::eFragment, sizeof(MaterialUniforms)) // mat factors
    );

    DescriptorAllocator descriptor_allocator(device);
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
        device.updateDescriptorSets({
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

    auto [position_buf, pb_alloc] = allocator.createBufferUnique({.size = gltf_data.vertex_position_data.size(), .usage = buffer_usage},
                                                                 allocation_create_info);
    auto [normal_buf, nb_alloc] = allocator.createBufferUnique({.size = gltf_data.vertex_normal_data.size(), .usage = buffer_usage},
                                                               allocation_create_info);
    auto [tangent_buf, tb_alloc] = allocator.createBufferUnique({.size = gltf_data.vertex_tangent_data.size(), .usage = buffer_usage},
                                                                allocation_create_info);
    auto [texcoord_buf, tcb_alloc] = allocator.createBufferUnique({.size = gltf_data.vertex_texcoord_data.size(), .usage = buffer_usage},
                                                                  allocation_create_info);
    auto [index_buf, ib_alloc] = allocator.createBufferUnique({
                                                                  .size = gltf_data.index_data.size(),
                                                                  .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                                                                           vk::BufferUsageFlagBits::eTransferDst
                                                              }, allocation_create_info); {
        auto staging_cmd_buf = transfer_commands.create();
        staging_uploader.upload(staging_cmd_buf, gltf_data.vertex_position_data, *position_buf);
        staging_uploader.upload(staging_cmd_buf, gltf_data.vertex_normal_data, *normal_buf);
        staging_uploader.upload(staging_cmd_buf, gltf_data.vertex_tangent_data, *tangent_buf);
        staging_uploader.upload(staging_cmd_buf, gltf_data.vertex_texcoord_data, *texcoord_buf);
        staging_uploader.upload(staging_cmd_buf, gltf_data.index_data, *index_buf);
        transfer_commands.submit(staging_cmd_buf);
        staging_uploader.releaseAll();
    }

    auto uniform_buffers = frameResources.create([&] {
        vma::AllocationInfo ub_alloc_info = {};
        auto [uniform_buffer, ub_alloc] = allocator.createBufferUnique(
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
        device.updateDescriptorSets({
                                        scene_descriptor_sets.get(i).write(0).setBufferInfo(uniform_buffer_info)
                                    }, {});
    }

    auto draw_command_pools = frameResources.create([&]() {
        return std::make_unique<CommandPool>(device, ctx.device.mainQueue, ctx.device.mainQueueFamily, CommandPool::UseMode::Reset);
    });

    auto loader = ShaderLoader();
#ifndef NDEBUG
    loader.debug = true;
#endif
    std::array descriptor_set_layouts = {scene_descriptor_layout.get(), material_descriptor_layout.get()};
    vk::PushConstantRange push_constant_range = {.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)};
    std::array push_constant_ranges = {push_constant_range};

    const auto load_shader = [&]() {
        auto vert_sh = loader.load("assets/test.vert");
        auto frag_sh = loader.load("assets/test.frag");
        return Shader(device, {vert_sh, frag_sh}, descriptor_set_layouts, push_constant_ranges);
    };
    auto shader = load_shader();

    const auto create_semaphore = [&] { return device.createSemaphoreUnique(vk::SemaphoreCreateInfo{}); };
    auto image_available_semaphores = frameResources.create(create_semaphore);
    auto render_finished_semaphores = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [&] {
        return device.createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frameResources.create(create_signaled_fence);

    initImGui(ctx);

    Camera camera(90, 0.1, {}, {});
    FrameTimes frame_times = {};
    while (!ctx.window.get().shouldClose()) {
        frameResources.advance();
        auto in_flight_fence = in_flight_fences.get();
        while (device.waitForFences({in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        input.update();

        auto image_available_semaphore = image_available_semaphores.get();
        if (!swapchain.next(image_available_semaphore)) {
            continue;
        }
        // Reset fence once we are sure that we are submitting work
        device.resetFences({in_flight_fence});

        //
        // Start of rendering and application code
        //

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();

        if (input.isKeyPress(GLFW_KEY_F5)) {
            Logger::info("Reloading shader");
            try {
                shader = load_shader();
            } catch (const std::exception &exc) {
                Logger::error("Reload failed: " + std::string(exc.what()));
            }
        }

        if (input.isMouseReleased() && input.isMousePress(GLFW_MOUSE_BUTTON_LEFT)) {
            if (!ImGui::GetIO().WantCaptureMouse)
                input.captureMouse();
        } else if (input.isMouseCaptured() && input.isKeyPress(GLFW_KEY_LEFT_ALT)) {
            input.releaseMouse();
        }

        if (input.isMouseCaptured()) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

        if (input.isMouseCaptured()) {
            // yaw
            camera.angles.y -= input.mouseDelta().x * glm::radians(0.15f);
            camera.angles.y = glm::wrapAngle(camera.angles.y);

            // pitch
            camera.angles.x -= input.mouseDelta().y * glm::radians(0.15f);
            camera.angles.x = glm::clamp(camera.angles.x, -glm::half_pi<float>(), glm::half_pi<float>());

            glm::vec3 move_input = {
                input.isKeyDown(GLFW_KEY_D) - input.isKeyDown(GLFW_KEY_A),
                input.isKeyDown(GLFW_KEY_SPACE) - input.isKeyDown(GLFW_KEY_LEFT_CONTROL),
                input.isKeyDown(GLFW_KEY_S) - input.isKeyDown(GLFW_KEY_W)
            };
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

        ImageRef color_attachment(swapchain.colorImage(), swapchain.colorFormatSrgb(), {
                                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  });
        ImageRef depth_attachment(swapchain.depthImage(), swapchain.depthFormat(), {
                                      .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  });

        auto &draw_commands = draw_command_pools.get();
        draw_commands.reset();
        auto cmd_buf = draw_commands.create();
        cmd_buf.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        color_attachment.barrier(cmd_buf, ImageResourceAccess::COLOR_ATTACHMENT_WRITE);
        depth_attachment.barrier(cmd_buf, ImageResourceAccess::DEPTH_ATTACHMENT_READ, ImageResourceAccess::DEPTH_ATTACHMENT_WRITE);

        vk::RenderingAttachmentInfoKHR color_attachment_info{
            .imageView = swapchain.colorViewSrgb(),
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
        cmd_buf.beginRendering(redering_info);
        cmd_buf.bindShadersEXT(shader.stages(), shader.shaders());

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
        pipeline_config.apply(cmd_buf, shader.stageFlags());

        cmd_buf.bindVertexBuffers(0, {*position_buf, *normal_buf, *tangent_buf, *texcoord_buf}, {0, 0, 0, 0});
        cmd_buf.bindIndexBuffer(*index_buf, 0, vk::IndexType::eUint32);
        shader.bindDescriptorSet(cmd_buf, 0, scene_descriptor_sets.get().get());

        for (const auto &instance: gltf_data.instances) {
            shader.bindDescriptorSet(cmd_buf, 1, material_descriptors[instance.material.index].get());

            cmd_buf.pushConstants(shader.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation);
            cmd_buf.drawIndexed(instance.indexCount, 1, instance.indexOffset, instance.vertexOffset, 0);
        }
        cmd_buf.endRendering();

        frame_times.update(input.timeDelta());
        frame_times.draw();

        // draw imgui last
        color_attachment_info.imageView = swapchain.colorViewLinear();
        color_attachment_info.loadOp = vk::AttachmentLoadOp::eLoad;
        depth_attachment_info.loadOp = vk::AttachmentLoadOp::eLoad;
        cmd_buf.beginRendering(redering_info);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buf);
        cmd_buf.endRendering();

        color_attachment.barrier(cmd_buf, ImageResourceAccess::PRESENT_SRC);

        cmd_buf.end();

        auto render_finished_semaphore = render_finished_semaphores.get();
        vk::PipelineStageFlags pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit_info = vk::SubmitInfo()
                .setCommandBuffers(cmd_buf)
                .setWaitSemaphores(image_available_semaphore)
                .setWaitDstStageMask(pipe_stage_flags)
                .setSignalSemaphores(render_finished_semaphore);
        ctx.device.mainQueue.submit({submit_info}, in_flight_fence);

        swapchain.present(ctx.device.mainQueue, vk::PresentInfoKHR().setWaitSemaphores(render_finished_semaphore));
    }

    Logger::info("Exited main loop");
    device.waitIdle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
