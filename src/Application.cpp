#include "Application.h"

#include "GraphicsBackend.h"
#include "Logger.h"
#include "ShaderLoader.h"
#include "Allocator.h"

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

template<std::ranges::contiguous_range R>
static std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> createBufferWithMemory(
    const vk::PhysicalDevice physical_device, const vk::Device &device, R &&data, vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties) {
    using T = std::ranges::range_value_t<R>;
    vk::UniqueBuffer buffer = device.createBufferUnique({
        .size = data.size() * sizeof(T),
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    });
    vk::MemoryRequirements requirements = device.getBufferMemoryRequirements(*buffer);
    vk::PhysicalDeviceMemoryProperties available_properties = physical_device.getMemoryProperties();

    // select largest fitting memory
    uint32_t memory_type_index = -1;
    vk::DeviceSize memory_heap_size = 0;
    for (uint32_t i = 0; i < available_properties.memoryTypeCount; i++) {
        uint32_t bitmask = requirements.memoryTypeBits;
        uint32_t bit = 1 << i;
        if ((bitmask & bit) == 0) {
            continue;
        }

        vk::MemoryType memory_type = available_properties.memoryTypes[i];
        if ((memory_type.propertyFlags & properties) != properties) {
            continue;
        }

        vk::DeviceSize heapSize = available_properties.memoryHeaps[memory_type.heapIndex].size;
        if (heapSize > memory_heap_size) {
            memory_type_index = i;
            memory_heap_size = heapSize;
        }
    }

    if (memory_type_index == -1) {
        Logger::panic("No suitable memory type for buffer found");
    }

    vk::UniqueDeviceMemory memory = device.allocateMemoryUnique({
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type_index
    });

    device.bindBufferMemory(*buffer, *memory, 0);

    return std::pair(std::move(buffer), std::move(memory));
}

// template <std::ranges::contiguous_range R>
// static vk::UniqueBuffer createBufferWithMemoryVma(R&& data, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
//     using T = std::ranges::range_value_t<R>;
//     auto [buffer, alloc] = VulkanAllocator.createBuffer({
//         .size = data.size() * sizeof(T),
//         .usage = usage,
//         .sharingMode = vk::SharingMode::eExclusive,
//     }, {
//         .usage = vma::MemoryUsage::eAutoPreferDevice,
//         .requiredFlags = properties,
//     });
// }

void Application::run() {
    auto &device = backend->device;

    const std::vector<Vertex> vertices = {
        {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    auto [vbo, vbo_mem] = createBufferWithMemory(backend->phyicalDevice, *backend->device, vertices,
                                                 vk::BufferUsageFlagBits::eVertexBuffer,
                                                 vk::MemoryPropertyFlagBits::eHostVisible |
                                                 vk::MemoryPropertyFlagBits::eHostCoherent); {
        auto *vbo_mapped_mem = static_cast<Vertex *>(device->mapMemory(*vbo_mem, 0,
                                                                       vertices.size() * sizeof(vertices[0])));
        std::memcpy(vbo_mapped_mem, vertices.data(), vertices.size() * sizeof(vertices[0]));
        device->unmapMemory(*vbo_mem);
    }

    loader = std::make_unique<ShaderLoader>(backend->device);
    auto vert_sh = loader->load("assets/test.vert");
    auto frag_sh = loader->load("assets/test.frag");
    auto [pipeline_layout, pipeline] =
        loader->link(*backend->renderPass, {vert_sh, frag_sh}, Vertex::bindingDescriptors(), Vertex::attributeDescriptors(), {});

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

        command_buffer.draw(3, 1, 0, 0);
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


