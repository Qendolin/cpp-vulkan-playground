#include "Application.h"

#include "GraphicsBackend.h"
#include "ShaderLoader.h"

Application::Application() {
    backend = std::make_unique<GraphicsBackend>();
    backend->createSwapchain();
    backend->createRenderPass();
    backend->createCommandBuffers();
}

Application::~Application() = default;


void Application::run() {
    auto& device = backend->device;

    loader = std::make_unique<ShaderLoader>(backend->device);
    auto vert_sh = loader->load("assets/test.vert");
    auto frag_sh = loader->load("assets/test.frag");
    auto [pipeline_layout, pipeline] = loader->link(*backend->renderPass, {vert_sh, frag_sh}, {});



    auto image_available_semaphore = device->createSemaphoreUnique(vk::SemaphoreCreateInfo{});
    auto render_finished_semaphore = device->createSemaphoreUnique(vk::SemaphoreCreateInfo{});
    auto in_flight_fence = device->createFenceUnique(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});

    while(!backend->window->shouldClose()) {
        while(device->waitForFences({*in_flight_fence}, true, UINT64_MAX) == vk::Result::eTimeout) {}
        device->resetFences({*in_flight_fence});
        glfwPollEvents();

        auto image_index = device->acquireNextImageKHR(*backend->swapchain, UINT64_MAX, *image_available_semaphore, nullptr).value;

        auto& command_buffer = *backend->commandBuffer;
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

        command_buffer.setViewport(0, {{0.0f, 0.0f, static_cast<float>(backend->surfaceExtents.width), static_cast<float>(backend->surfaceExtents.height), 0.0f, 1.0f}});
        command_buffer.setScissor(0, {{{}, backend->surfaceExtents}});
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setDepthTestEnable(false);
        command_buffer.setDepthWriteEnable(true);
        command_buffer.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
        command_buffer.setStencilTestEnable(false);
        command_buffer.setStencilOp(vk::StencilFaceFlagBits::eFrontAndBack, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eNever);
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRenderPass();
        command_buffer.end();

        vk::PipelineStageFlags const pipe_stage_flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit_info = vk::SubmitInfo()
            .setCommandBuffers(command_buffer)
            .setWaitSemaphores(*image_available_semaphore)
            .setWaitDstStageMask(pipe_stage_flags)
            .setSignalSemaphores(*render_finished_semaphore);
        backend->graphicsQueue.submit({submit_info}, *in_flight_fence);

        vk::PresentInfoKHR present_info = vk::PresentInfoKHR()
        .setWaitSemaphores(*render_finished_semaphore)
        .setSwapchains(*backend->swapchain)
        .setImageIndices(image_index);

        backend->graphicsQueue.presentKHR(present_info);
    }

    device->waitIdle();
}


