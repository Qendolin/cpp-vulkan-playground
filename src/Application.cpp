#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderLoader.h"

#include <glm/glm.hpp>

Application::Application() {
    backend = std::make_unique<GraphicsBackend>();
    backend->createSwapchain();
    backend->createRenderPass();
    backend->createCommandBuffers(frameResources.size());
}

Application::~Application() = default;

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

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
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, pos)
            },
            vk::VertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color)
            }
        };
        return desc;
    }
};

void Application::run() {
    auto &device = backend->device;

    const std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {
        0, 1, 2, 2, 3, 0
    };

    vk::DeviceSize vbo_size = vertices.size() * sizeof(vertices[0]);
    auto [vbo, vbo_mem] = backend->allocator->createBufferUnique(
        {
            .size = vbo_size,
            .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        }, {
            .usage = vma::MemoryUsage::eAutoPreferDevice,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });
    vk::DeviceSize ibo_size = indices.size() * sizeof(indices[0]);
    auto [ibo, ibo_mem] = backend->allocator->createBufferUnique(
        {
            .size = ibo_size,
            .usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        }, {
            .usage = vma::MemoryUsage::eAutoPreferDevice,
            .requiredFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
        });

    backend->uploadWithStaging(vertices, *vbo);
    backend->uploadWithStaging(indices, *ibo);

    loader = std::make_unique<ShaderLoader>(backend->device);
    auto vert_sh = loader->load("assets/test.vert");
    auto frag_sh = loader->load("assets/test.frag");
    auto [pipeline_layout, pipeline] =
            loader->link(*backend->renderPass, {vert_sh, frag_sh}, Vertex::bindingDescriptors(),
                         Vertex::attributeDescriptors(), {});

    const auto create_semaphore = [device] { return device->createSemaphoreUnique(vk::SemaphoreCreateInfo{}); };
    auto image_available_semaphore_pool = frameResources.create(create_semaphore);
    auto render_finished_semaphore_pool = frameResources.create(create_semaphore);
    const auto create_signaled_fence = [device] {
        return device->createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    };
    auto in_flight_fence_pool = frameResources.create(create_signaled_fence);

    while (!backend->window->shouldClose()) {
        frameResources.advance();
        auto in_flight_fence = in_flight_fence_pool.get();
        while (device->waitForFences({in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {
        }
        glfwPollEvents();

        auto image_available_semaphore = image_available_semaphore_pool.get();
        auto render_finished_semaphore = render_finished_semaphore_pool.get();

        uint32_t image_index = 0;
        try {
            auto image_acquistion_result = device->acquireNextImageKHR(*backend->swapchain, UINT64_MAX,
                                                                       image_available_semaphore, nullptr);
            if (image_acquistion_result.result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
            }
            image_index = image_acquistion_result.value;
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            // TODO: https://vulkan-tutorial.com/Drawing_a_triangle/Swap_chain_recreation#page_Handling-resizes-explicitly
            backend->recreateSwapchain();
            continue;
        }

        // Reset fence once we are sure that we are submitting work
        device->resetFences({in_flight_fence});

        auto &command_buffer = *backend->commandBuffers.at(frameResources.frame());
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{});

        vk::ClearValue clear_color = {{{{0.0f, 0.0f, 0.0f, 1.0f}}}};
        vk::RenderPassBeginInfo render_pass_begin_info = {
            .renderPass = *backend->renderPass,
            .framebuffer = *backend->framebuffers[image_index],
            .renderArea = {
                .offset = {},
                .extent = backend->surfaceExtents
            },
            .clearValueCount = 1,
            .pClearValues = &clear_color,
        };
        command_buffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

        command_buffer.setViewport(0, {
                                       {
                                           0.0f, 0.0f, static_cast<float>(backend->surfaceExtents.width),
                                           static_cast<float>(backend->surfaceExtents.height), 0.0f, 1.0f
                                       }
                                   });
        command_buffer.setScissor(0, {{{}, backend->surfaceExtents}});
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setDepthTestEnable(false);
        command_buffer.setDepthWriteEnable(true);
        command_buffer.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
        command_buffer.setStencilTestEnable(false);
        command_buffer.setStencilOp(vk::StencilFaceFlagBits::eFrontAndBack, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                    vk::StencilOp::eKeep, vk::CompareOp::eNever);

        command_buffer.bindVertexBuffers(0, {*vbo}, {0});
        command_buffer.bindIndexBuffer(*ibo, 0, vk::IndexType::eUint16);

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

        try {
            vk::Result result = backend->graphicsQueue.presentKHR(present_info);
            if (result == vk::Result::eSuboptimalKHR) {
                Logger::warning("Swapchain may need recreation: VK_SUBOPTIMAL_KHR");
            }
        } catch (const vk::OutOfDateKHRError &) {
            Logger::warning("Swapchain needs recreation: VK_ERROR_OUT_OF_DATE_KHR");
            backend->recreateSwapchain();
            continue;
        }
    }

    Logger::info("Exited main loop");
    device->waitIdle();
}
