#include "Application.h"

#include <cstring>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>
#include <tracy/Tracy.hpp>

#include "Camera.h"
#include "CommandPool.h"
#include "Descriptors.h"
#include "FrameResource.h"
#include "GraphicsBackend.h"
#include "Image.h"
#include "Logger.h"
#include "ShaderObject.h"
#include "StagingBuffer.h"
#include "Swapchain.h"
#include "UniformBuffer.h"
#include "debug/Performance.h"
#include "glfw/Input.h"
#include "gltf/Gltf.h"
#include "imgui/ImGui.h"

Application::Application() = default;

Application::~Application() = default;

// Utility type for uniforms structs, ensures sequential write on assignment
#define MEMCPY_ASSIGNMENT(T)                                                                                           \
    T &operator=(const T &other) {                                                                                     \
        if (this != &other) {                                                                                          \
            std::memcpy(this, &other, sizeof(T)); /* NOLINT(*-undefined-memory-manipulation) */                        \
        }                                                                                                              \
        return *this;                                                                                                  \
    }

#if defined(__clang__)
#define TRIVIAL_ABI [[clang::trivial_abi]]
#elif defined(__GNUC__) || defined(__GNUG__)
#define TRIVIAL_ABI [[gnu::trivial_abi]]
#else
#define TRIVIAL_ABI
#endif

struct TRIVIAL_ABI alignas(16) SceneUniforms {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camera;

    MEMCPY_ASSIGNMENT(SceneUniforms);
};

struct TRIVIAL_ABI alignas(16) MaterialUniforms {
    glm::vec4 albedoFectors;
    glm::vec4 mrnFactors;

    MEMCPY_ASSIGNMENT(MaterialUniforms)
};

inline Image load_image(Commands &commands, IStagingBuffer &staging, const PlainImageData &data) {
    auto [buffer, ptr] = staging.upload(commands, data.pixels.size_bytes(), data.pixels.data());
    Image image = Image::create(staging.allocator(), ImageCreateInfo::from(data));
    image.barrier(*commands, ImageResourceAccess::TRANSFER_WRITE);
    image.load(*commands, 0, {}, buffer);
    commands.trash += buffer;
    return image;
}

struct SceneUploadData {
    vk::UniqueSampler sampler;
    std::vector<Image> images;
    std::vector<vk::UniqueImageView> views;

    std::vector<DescriptorSet> descriptors;

    Image defaultAlbedo;
    vk::UniqueImageView defaultAlbedoView;
    Image defaultNormal;
    vk::UniqueImageView defaultNormalView;
    Image defaultOmr;
    vk::UniqueImageView defaultOmrView;

    vma::UniqueBuffer positions;
    vma::UniqueAllocation positionsAlloc;
    vma::UniqueBuffer normals;
    vma::UniqueAllocation normalsAlloc;
    vma::UniqueBuffer tangents;
    vma::UniqueAllocation tangentsAlloc;
    vma::UniqueBuffer texcoords;
    vma::UniqueAllocation texcoordsAlloc;
    vma::UniqueBuffer indices;
    vma::UniqueAllocation indicesAlloc;
};


inline auto create_default_resources(Commands &commands, IStagingBuffer &staging) {
    std::vector<uint8_t> albedo_pixels(16 * 16 * 4);
    std::ranges::fill(albedo_pixels, 0xff);
    Image default_albedo = load_image(commands, staging, {albedo_pixels, 16, 16, vk::Format::eR8G8B8A8Unorm});
    default_albedo.generateMipmaps(*commands);

    std::vector<uint8_t> normal_pixels(16 * 16 * 2);
    std::ranges::fill(normal_pixels, 0x7f);
    Image default_normal = load_image(commands, staging, {normal_pixels, 16, 16, vk::Format::eR8G8Unorm});
    default_normal.generateMipmaps(*commands);

    std::vector<uint8_t> omr_pixels(16 * 16 * 4);
    std::ranges::fill(omr_pixels, 0xff);
    Image default_omr = load_image(commands, staging, {omr_pixels, 16, 16, vk::Format::eR8G8B8A8Unorm});
    default_omr.generateMipmaps(*commands);

    return std::tuple{std::move(default_albedo), std::move(default_normal), std::move(default_omr)};
}

inline SceneUploadData upload_gltf_data(
        const AppContext &ctx, gltf::SceneData &gltf_data, DescriptorAllocator &descriptor_allocator, DescriptorSetLayout &descriptor_layout
) {
    SceneUploadData result;

    const auto &allocator = *ctx.device.allocator;
    const auto &device = ctx.device.get();

    auto staging = DoubleStagingBuffer(*ctx.device.allocator, device, 64000000);
    auto commands = Commands(device, ctx.device.mainQueue, ctx.device.mainQueueFamily, Commands::UseMode::Single);
    commands.begin();

    std::tie(result.defaultAlbedo, result.defaultNormal, result.defaultOmr) = create_default_resources(commands, staging);
    result.defaultAlbedoView = result.defaultAlbedo.createDefaultView(device);
    result.defaultNormalView = result.defaultNormal.createDefaultView(device);
    result.defaultOmrView = result.defaultOmr.createDefaultView(device);

    float max_anisotropy = ctx.device.physicalDevice.getProperties().limits.maxSamplerAnisotropy;
    result.sampler = device.createSamplerUnique(
            {.magFilter = vk::Filter::eLinear,
             .minFilter = vk::Filter::eLinear,
             .mipmapMode = vk::SamplerMipmapMode::eLinear,
             .anisotropyEnable = true,
             .maxAnisotropy = max_anisotropy,
             .maxLod = vk::LodClampNone,
             .borderColor = vk::BorderColor::eFloatOpaqueBlack}
    );

    result.images.reserve(gltf_data.images.size());
    result.views.reserve(gltf_data.images.size());
    for (const auto &image_data: gltf_data.images) {
        if (image_data.pixels.empty()) {
            result.views.emplace_back() = result.defaultAlbedo.createDefaultView(device);
            continue;
        }
        auto &image = result.images.emplace_back();
        image = load_image(commands, staging, image_data);
        image.generateMipmaps(*commands);
        image.barrier(*commands, ImageResourceAccess::FRAGMENT_SHADER_READ);

        result.views.emplace_back() = image.createDefaultView(device);
    }

    commands.submit();
    commands.begin();

    result.descriptors.reserve(gltf_data.materials.size());
    for (auto &material: gltf_data.materials) {
        result.descriptors.emplace_back() = descriptor_allocator.allocate(descriptor_layout);
        const auto &descriptor_set = result.descriptors.back();
        vk::DescriptorImageInfo albedo_image_info = {
            .sampler = *result.sampler,
            .imageView = material.albedo == -1 ? *result.defaultAlbedoView : *result.views.at(material.albedo),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo normal_image_info = {
            .sampler = *result.sampler,
            .imageView = material.normal == -1 ? *result.defaultNormalView : *result.views.at(material.normal),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        vk::DescriptorImageInfo orm_image_info = {
            .sampler = *result.sampler,
            .imageView = material.omr == -1 ? *result.defaultOmrView : *result.views.at(material.omr),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        };
        MaterialUniforms material_uniforms = {
            .albedoFectors = material.albedoFactor,
            .mrnFactors = glm::vec4(material.metaillicFactor, material.roughnessFactor, material.normalFactor, 0.0),
        };
        vk::WriteDescriptorSetInlineUniformBlock mat_info = {.dataSize = sizeof(MaterialUniforms), .pData = &material_uniforms};
        device.updateDescriptorSets(
                {
                    descriptor_set.write(0).setPImageInfo(&albedo_image_info),
                    descriptor_set.write(1).setPImageInfo(&normal_image_info),
                    descriptor_set.write(2).setPImageInfo(&orm_image_info),
                    descriptor_set.write(3).setPNext(&mat_info),
                },
                {}
        );
    }

    vma::AllocationCreateInfo allocation_create_info = {
        .usage = vma::MemoryUsage::eAutoPreferDevice,
        .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
    };
    auto buffer_usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;

    std::tie(result.positions, result.positionsAlloc) = allocator.createBufferUnique(
            {.size = gltf_data.vertex_position_data.size(), .usage = buffer_usage}, allocation_create_info
    );
    std::tie(result.normals, result.normalsAlloc) = allocator.createBufferUnique(
            {.size = gltf_data.vertex_normal_data.size(), .usage = buffer_usage}, allocation_create_info
    );
    std::tie(result.tangents, result.tangentsAlloc) = allocator.createBufferUnique(
            {.size = gltf_data.vertex_tangent_data.size(), .usage = buffer_usage}, allocation_create_info
    );
    std::tie(result.texcoords, result.texcoordsAlloc) = allocator.createBufferUnique(
            {.size = gltf_data.vertex_texcoord_data.size(), .usage = buffer_usage}, allocation_create_info
    );
    std::tie(result.indices, result.indicesAlloc) = allocator.createBufferUnique(
            {.size = gltf_data.index_data.size(),
             .usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst},
            allocation_create_info
    );

    staging.upload(commands, gltf_data.vertex_position_data, *result.positions);
    staging.upload(commands, gltf_data.vertex_normal_data, *result.normals);
    staging.upload(commands, gltf_data.vertex_tangent_data, *result.tangents);
    staging.upload(commands, gltf_data.vertex_texcoord_data, *result.texcoords);
    staging.upload(commands, gltf_data.index_data, *result.indices);
    commands.submit();
    return result;
}

void Application::run() {
    auto ctx = AppContext({.width = 1600, .height = 900, .title = "Vulkan Playground"});
    auto frameResources = FrameResourceManager(ctx.swapchain->imageCount());

    const auto device = ctx.device.get();
    auto &allocator = *ctx.device.allocator;
    auto &input = *ctx.window.input;
    auto &swapchain = *ctx.swapchain;
    assert(ctx.swapchain != nullptr); // clion/clangd needs this
    auto transfer_commands =
            CommandPool(device, ctx.device.transferQueue, ctx.device.transferQueueFamily, CommandPool::UseMode::Single);
    auto one_time_commands =
            CommandPool(device, ctx.device.mainQueue, ctx.device.mainQueueFamily, CommandPool::UseMode::Single);

    // TracyVkCtx(
    //     static_cast<VkPhysicalDevice>(ctx.device.physicalDevice),
    //     static_cast<VkDevice>(ctx.device.get()),
    //     static_cast<VkQueue>(ctx.device.mainQueue),
    //     static_cast<VkCommandBuffer>()
    // );

    DescriptorAllocator descriptor_allocator(device);
    DescriptorSetLayout scene_descriptor_layout(
            device, {},
            DescriptorBinding(
                    0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            ) // uniforms
    );
    DescriptorSetLayout material_descriptor_layout(
            device, {},
            DescriptorBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // albedo
            DescriptorBinding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // normal
            DescriptorBinding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment), // omr
            DescriptorBinding(
                    3, vk::DescriptorType::eInlineUniformBlock, vk::ShaderStageFlagBits::eFragment, sizeof(MaterialUniforms)
            ) // material factors
    );

    gltf::SceneData gltf_data = gltf::load("assets/sponza.glb");
    auto scene_data = upload_gltf_data(ctx, gltf_data, descriptor_allocator, material_descriptor_layout);

    auto uniform_buffers = frameResources.create([&] { return UnifromBuffer<SceneUniforms>(allocator); });

    auto scene_descriptor_sets = frameResources.create([&descriptor_allocator, &scene_descriptor_layout] {
        return descriptor_allocator.allocate(scene_descriptor_layout);
    });

    for (int i = 0; i < frameResources.size(); ++i) {
        vk::DescriptorBufferInfo uniform_buffer_info = {
            .buffer = uniform_buffers.at(i).buffer(), .offset = 0, .range = sizeof(SceneUniforms)
        };
        device.updateDescriptorSets({scene_descriptor_sets.at(i).write(0).setBufferInfo(uniform_buffer_info)}, {});
    }

    auto draw_command_pools = frameResources.create([&]() {
        return std::make_unique<CommandPool>(
                device, ctx.device.mainQueue, ctx.device.mainQueueFamily, CommandPool::UseMode::Reset
        );
    });

    auto loader = ShaderLoader();
#ifndef NDEBUG
    loader.debug = true;
#endif
    std::array descriptor_set_layouts = {scene_descriptor_layout.get(), material_descriptor_layout.get()};
    vk::PushConstantRange push_constant_range = {
        .stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)
    };
    std::array push_constant_ranges = {push_constant_range};

    const auto load_shader = [&]() {
        auto vert_sh = loader.load("assets/test.vert");
        auto frag_sh = loader.load("assets/test.frag");
        return Shader(device, {vert_sh, frag_sh}, descriptor_set_layouts, push_constant_ranges);
    };
    auto shader = load_shader();

    const auto create_semaphore = [&] {
        return device.createSemaphoreUnique(vk::SemaphoreCreateInfo{});
    };
    auto image_available_semaphores = frameResources.create(create_semaphore);
    auto render_finished_semaphores = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [&] {
        return device.createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frameResources.create(create_signaled_fence);

    initImGui(ctx);

    FrameMark;

    Camera camera(90, 0.1, {}, {});
    FrameTimes frame_times = {};
    while (!ctx.window.get().shouldClose()) {
        frameResources.advance();
        auto &in_flight_fence = in_flight_fences.current();
        while (device.waitForFences(in_flight_fence, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        input.update();

        auto &image_available_semaphore = image_available_semaphores.current();
        if (!swapchain.advance(image_available_semaphore)) {
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
            .view = camera.viewMatrix(), .proj = camera.projectionMatrix(), .camera = glm::vec4(camera.position, 1.0)
        };

        uniform_buffers->front() = uniforms;

        ImageRef color_attachment(
                swapchain.colorImage(), swapchain.colorFormatSrgb(),
                {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1}
        );
        ImageRef depth_attachment(
                swapchain.depthImage(), swapchain.depthFormat(),
                {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1}
        );

        auto &draw_commands = draw_command_pools.current();
        draw_commands.reset();
        auto cmd_buf = draw_commands.create();
        cmd_buf.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        color_attachment.barrier(cmd_buf, ImageResourceAccess::COLOR_ATTACHMENT_WRITE);
        depth_attachment.barrier(
                cmd_buf, ImageResourceAccess::DEPTH_ATTACHMENT_READ, ImageResourceAccess::DEPTH_ATTACHMENT_WRITE
        );

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
            .viewports = {{vk::Viewport{0.0f, swapchain.height(), swapchain.width(), -swapchain.height(), 0.0f, 1.0f}}},
            .scissors = {{swapchain.area()}},
            .cullMode = vk::CullModeFlagBits::eNone,
            .frontFace = vk::FrontFace::eCounterClockwise, // TODO: why CCW?!?
            .depthCompareOp = vk::CompareOp::eGreaterOrEqual
        };
        pipeline_config.apply(cmd_buf, shader.stageFlags());

        cmd_buf.bindVertexBuffers(
                0, {*scene_data.positions, *scene_data.normals, *scene_data.tangents, *scene_data.texcoords}, {0, 0, 0, 0}
        );
        cmd_buf.bindIndexBuffer(*scene_data.indices, 0, vk::IndexType::eUint32);
        shader.bindDescriptorSet(cmd_buf, 0, scene_descriptor_sets.current().get());

        for (const auto &instance: gltf_data.instances) {
            shader.bindDescriptorSet(cmd_buf, 1, scene_data.descriptors[instance.material.index].get());

            cmd_buf.pushConstants(
                    shader.pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation
            );
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

        auto &render_finished_semaphore = render_finished_semaphores.current();
        vk::PipelineStageFlags pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit_info = vk::SubmitInfo()
                                             .setCommandBuffers(cmd_buf)
                                             .setWaitSemaphores(image_available_semaphore)
                                             .setWaitDstStageMask(pipe_stage_flags)
                                             .setSignalSemaphores(render_finished_semaphore);
        ctx.device.mainQueue.submit({submit_info}, in_flight_fence);

        swapchain.present(ctx.device.mainQueue, vk::PresentInfoKHR().setWaitSemaphores(render_finished_semaphore));
        FrameMark;
    }

    Logger::info("Exited main loop");
    device.waitIdle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
