#include "Application.h"

#include <cstring>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>
#include <vulkan/vulkan.hpp>

#include "Camera.h"
#include "CommandPool.h"
#include "Descriptors.h"
#include "FrameResource.h"
#include "Framebuffer.h"
#include "GraphicsBackend.h"
#include "Image.h"
#include "Logger.h"
#include "ShaderObject.h"
#include "StagingBuffer.h"
#include "Swapchain.h"
#include "UniformBuffer.h"
#include "debug/Performance.h"
#include "debug/Tracy.h"
#include "glfw/Input.h"
#include "gltf/Gltf.h"
#include "imgui/ImGui.h"
#include "util/buffer_struct.h"

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
    image.barrier(*commands, ImageResourceAccess::TransferWrite);
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

struct MaterialDescriptorSetLayout : DescriptorSetLayoutBase {
    static constexpr auto Albedo = combinedImageSampler(0, ShaderStage::eFragment);
    static constexpr auto Normal = combinedImageSampler(1, ShaderStage::eFragment);
    static constexpr auto Omr = combinedImageSampler(2, ShaderStage::eFragment);
    static constexpr auto MaterialFactors = inlineUniformBlock(3, ShaderStage::eFragment, sizeof(MaterialUniforms));

    inline static const auto Bindings = validate(Albedo, Normal, Omr, MaterialFactors);

    explicit MaterialDescriptorSetLayout(const vk::Device &device, vk::DescriptorSetLayoutCreateFlags flags = {})
        : DescriptorSetLayoutBase(device, flags, Bindings) {}

    ~MaterialDescriptorSetLayout() override = default;
};

struct SceneDescriptorSetLayout : DescriptorSetLayoutBase {
    static constexpr auto SceneUniforms = uniformBuffer(0, ShaderStage::eVertex | ShaderStage::eFragment);

    inline static const auto Bindings = validate(SceneUniforms);

    explicit SceneDescriptorSetLayout(const vk::Device &device, vk::DescriptorSetLayoutCreateFlags flags = {})
        : DescriptorSetLayoutBase(device, flags, Bindings) {}

    ~SceneDescriptorSetLayout() override {}
};

inline SceneUploadData upload_gltf_data(
        const AppContext &ctx, gltf::SceneData &gltf_data, DescriptorAllocator &descriptor_allocator
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
        image.barrier(*commands, ImageResourceAccess::FragmentShaderRead);

        result.views.emplace_back() = image.createDefaultView(device);
    }

    commands.submit();
    commands.begin();

    auto descriptor_layout = MaterialDescriptorSetLayout(device);
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
        vk::DescriptorImageInfo omr_image_info = {
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
                    descriptor_set.write(MaterialDescriptorSetLayout::Albedo, albedo_image_info),
                    descriptor_set.write(MaterialDescriptorSetLayout::Normal, normal_image_info),
                    descriptor_set.write(MaterialDescriptorSetLayout::Omr, omr_image_info),
                    descriptor_set.write(MaterialDescriptorSetLayout::MaterialFactors, mat_info),
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

Application::Application(AppContext &ctx) : ctx(ctx), input_(*ctx.window.input) {};

Application::~Application() = default;

void Application::loadShader() {
    auto scene_layout = SceneDescriptorSetLayout(ctx.device.get());
    auto material_layout = MaterialDescriptorSetLayout(ctx.device.get());

    ShaderInterfaceLayout shader_layout = {
        .descriptorSetLayouts = {scene_layout.layout, material_layout.layout},
        .pushConstantRanges = {{.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(glm::mat4)}}
    };

    auto vert_sh = shaderLoader_->load("assets/shaders/test.vert");
    auto frag_sh = shaderLoader_->load("assets/shaders/test.frag");
    shader_ = std::make_unique<Shader>(
            ctx.device.get(), std::initializer_list<ShaderStage>{vert_sh, frag_sh},
            std::span(shader_layout.descriptorSetLayouts), std::span(shader_layout.pushConstantRanges)
    );
}


void Application::updateInput(Camera &camera) {
    ZoneScopedN("Input Update");
    if (input_.isKeyPress(GLFW_KEY_F5)) {
        Logger::info("Reloading shader");
        try {
            loadShader();
        } catch (const std::exception &exc) {
            Logger::error("Reload failed: " + std::string(exc.what()));
        }
    }

    if (input_.isMouseReleased() && input_.isMousePress(GLFW_MOUSE_BUTTON_LEFT)) {
        if (!ImGui::GetIO().WantCaptureMouse)
            input_.captureMouse();
    } else if (input_.isMouseCaptured() && input_.isKeyPress(GLFW_KEY_LEFT_ALT)) {
        input_.releaseMouse();
    }

    if (input_.isMouseCaptured()) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    if (input_.isMouseCaptured()) {
        // yaw
        camera.angles.y -= input_.mouseDelta().x * glm::radians(0.15f);
        camera.angles.y = glm::wrapAngle(camera.angles.y);

        // pitch
        camera.angles.x -= input_.mouseDelta().y * glm::radians(0.15f);
        camera.angles.x = glm::clamp(camera.angles.x, -glm::half_pi<float>(), glm::half_pi<float>());

        glm::vec3 move_input = {
            input_.isKeyDown(GLFW_KEY_D) - input_.isKeyDown(GLFW_KEY_A),
            input_.isKeyDown(GLFW_KEY_SPACE) - input_.isKeyDown(GLFW_KEY_LEFT_CONTROL),
            input_.isKeyDown(GLFW_KEY_S) - input_.isKeyDown(GLFW_KEY_W)
        };
        glm::vec3 velocity = move_input * 5.0f;
        velocity = glm::mat3(glm::rotate(glm::mat4(1.0), camera.angles.y, {0, 1, 0})) * velocity;
        camera.position += velocity * input_.timeDelta();
    }
    camera.updateViewMatrix();
}

void Application::run() {
    const auto device = ctx.device.get();
    auto &allocator = *ctx.device.allocator;
    auto &input = *ctx.window.input;
    auto &swapchain = *ctx.swapchain;
    assert(ctx.swapchain != nullptr); // clion/clangd needs this

    TracyContext::create(ctx.device.physicalDevice, device, ctx.device.mainQueue, ctx.device.mainQueueFamily);

    DescriptorAllocator descriptor_allocator(device);

    auto scene_descriptor_layout = SceneDescriptorSetLayout(device);

    gltf::SceneData gltf_data = gltf::load("assets/models/sponza.glb");
    auto scene_data = upload_gltf_data(ctx, gltf_data, descriptor_allocator);

    auto frame_resources = FrameResourceManager(ctx.swapchain->imageCount());
    auto uniform_buffers = frame_resources.create([&] { return UnifromBuffer<SceneUniforms>(allocator); });
    auto scene_descriptor_sets = frame_resources.create([&](int i) {
        auto set = descriptor_allocator.allocate(scene_descriptor_layout);
        vk::DescriptorBufferInfo uniform_buffer_info = {
            .buffer = uniform_buffers.at(i).buffer(), .offset = 0, .range = sizeof(SceneUniforms)
        };
        device.updateDescriptorSets({set.write(SceneDescriptorSetLayout::SceneUniforms, uniform_buffer_info)}, {});
        return set;
    });
    auto draw_command_pools = frame_resources.create([&]() {
        return std::make_unique<CommandPool>(
                device, ctx.device.mainQueue, ctx.device.mainQueueFamily, CommandPool::UseMode::Reset
        );
    });

    shaderLoader_ = std::make_unique<ShaderLoader>();
#ifndef NDEBUG
    shaderLoader_->debug = true;
#endif
    loadShader();

    const auto create_semaphore = [&] {
        return device.createSemaphoreUnique(vk::SemaphoreCreateInfo{});
    };
    auto image_available_semaphores = frame_resources.create(create_semaphore);
    auto render_finished_semaphores = frame_resources.create(create_semaphore);
    const auto create_signaled_fence = [&] {
        return device.createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fences = frame_resources.create(create_signaled_fence);
    auto framebuffers = frame_resources.create([&swapchain] {
        Framebuffer fb = {};
        fb.colorAttachments = {{
            .format = swapchain.colorFormatSrgb(),
            .range = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }};
        fb.depthAttachment = Attachment{
            .image = swapchain.depthImage(),
            .view = swapchain.depthView(),
            .format = swapchain.depthFormat(),
            .range = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
        };
        return fb;
    });

    auto im_gui_backend = ImGuiBackend(ctx.device, ctx.window.get(), *ctx.swapchain);

    FrameMark;

    Camera camera(90, 0.1f, {}, {});
    FrameTimes frame_times = {};
    while (!ctx.window.get().shouldClose()) {
        frame_resources.advance();
        auto &in_flight_fence = in_flight_fences.current();
        {
            ZoneScopedN("Wait Swapchain Fence");
            while (device.waitForFences(in_flight_fence, true, UINT64_MAX) == vk::Result::eTimeout) {
            }
            input.update();
        }

        auto &image_available_semaphore = image_available_semaphores.current();
        {
            ZoneScopedN("Advance Swapchain");
            if (!swapchain.advance(image_available_semaphore)) {
                continue;
            }

            // Reset fence once we are sure that we are submitting work
            device.resetFences({in_flight_fence});
        }

        //
        // Start of rendering and application code
        //

        im_gui_backend.begin();

        updateInput(camera);

        camera.setViewport(swapchain.width(), swapchain.height());
        uniform_buffers->front() = {
            .view = camera.viewMatrix(),
            .proj = camera.projectionMatrix(),
            .camera = glm::vec4(camera.position, 1.0),
        };

        vk::CommandBuffer cmd_buf;
        {
            ZoneScopedN("Reset Commands");
            auto &draw_commands = draw_command_pools.current();
            draw_commands.reset();
            cmd_buf = draw_commands.create();
            cmd_buf.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        }

        {
            TracyVkCollect(TracyContext::Vulkan, cmd_buf);
            TracyVkNamedZone(TracyContext::Vulkan, __tracy_cmd_buf_zone, cmd_buf, "Record Commands", true);
            ZoneScopedN("Record Commands");

            auto &framebuffer = framebuffers.current();
            framebuffer.colorAttachments[0].image = swapchain.colorImage();
            framebuffer.colorAttachments[0].view = swapchain.colorViewSrgb();
            framebuffer.barrierColor(cmd_buf, ImageResourceAccess::ColorAttachmentWrite);
            framebuffer.barrierDepth(
                    cmd_buf, ImageResourceAccess::DepthAttachmentRead, ImageResourceAccess::DepthAttachmentWrite
            );
            cmd_buf.beginRendering(framebuffer.renderingInfo(
                    swapchain.area(),
                    {.colorLoadOps = {vk::AttachmentLoadOp::eClear}, .depthLoadOp = vk::AttachmentLoadOp::eClear}
            ));

            PipelineConfig pipeline_config = {
                .vertexBindingDescriptions = gltf::Vertex::bindingDescriptors,
                .vertexAttributeDescriptions = gltf::Vertex::attributeDescriptors,
                .viewports = {{vk::Viewport{0.0f, swapchain.height(), swapchain.width(), -swapchain.height(), 0.0f, 1.0f}}},
                .scissors = {{swapchain.area()}},
                .cullMode = vk::CullModeFlagBits::eNone,
                .frontFace = vk::FrontFace::eCounterClockwise, // TODO: why CCW?!?
                .depthCompareOp = vk::CompareOp::eGreaterOrEqual
            };
            pipeline_config.apply(cmd_buf, shader_->stageFlags());

            cmd_buf.bindShadersEXT(shader_->stages(), shader_->shaders());
            cmd_buf.bindVertexBuffers(
                    0, {*scene_data.positions, *scene_data.normals, *scene_data.tangents, *scene_data.texcoords},
                    {0, 0, 0, 0}
            );
            cmd_buf.bindIndexBuffer(*scene_data.indices, 0, vk::IndexType::eUint32);
            shader_->bindDescriptorSet(cmd_buf, 0, scene_descriptor_sets.current().set);

            for (const auto &instance: gltf_data.instances) {
                shader_->bindDescriptorSet(cmd_buf, 1, scene_data.descriptors[instance.material.index].set);

                cmd_buf.pushConstants(
                        shader_->pipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &instance.transformation
                );
                cmd_buf.drawIndexed(instance.indexCount, 1, instance.indexOffset, instance.vertexOffset, 0);
            }
            cmd_buf.endRendering();

            frame_times.update(input.timeDelta());
            frame_times.draw();

            framebuffer.colorAttachments[0].view = swapchain.colorViewLinear();
            cmd_buf.beginRendering(framebuffer.renderingInfo(swapchain.area(), {}));
            im_gui_backend.render(cmd_buf);
            cmd_buf.endRendering();

            framebuffer.barrierColor(cmd_buf, ImageResourceAccess::PresentSrc);
        }
        cmd_buf.end();

        {
            ZoneScopedN("Submit & Present");
            auto &render_finished_semaphore = render_finished_semaphores.current();
            vk::PipelineStageFlags pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            vk::SubmitInfo submit_info = vk::SubmitInfo()
                                                 .setCommandBuffers(cmd_buf)
                                                 .setWaitSemaphores(image_available_semaphore)
                                                 .setWaitDstStageMask(pipe_stage_flags)
                                                 .setSignalSemaphores(render_finished_semaphore);
            ctx.device.mainQueue.submit({submit_info}, in_flight_fence);

            swapchain.present(ctx.device.mainQueue, vk::PresentInfoKHR().setWaitSemaphores(render_finished_semaphore));
        }
        FrameMark;
    }

    Logger::info("Exited main loop");
    device.waitIdle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    TracyContext::destroy(device);
}
