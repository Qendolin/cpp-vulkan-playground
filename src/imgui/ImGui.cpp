#include "ImGui.h"

#include <format>
#include <tracy/TracyVulkan.hpp>

#include "../GraphicsBackend.h"
#include "../Logger.h"
#include "../Swapchain.h"
#include "../debug/Tracy.h"

ImGuiBackend::ImGuiBackend(const DeviceContext &device, const glfw::Window &window, const Swapchain &swapchain) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(window), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = device.instace.get();
    init_info.PhysicalDevice = device.physicalDevice;
    init_info.Device = device.get();
    init_info.QueueFamily = device.mainQueueFamily;
    init_info.Queue = device.mainQueue;
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 512;
    init_info.MinImageCount = swapchain.minImageCount();
    init_info.ImageCount = swapchain.imageCount();
    init_info.UseDynamicRendering = true;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            Logger::panic(std::format("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(result))));
        }
    };
    vk::Format color_attachment_format = swapchain.colorFormatLinear();
    init_info.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_attachment_format,
        .depthAttachmentFormat = swapchain.depthFormat(),
    };

    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_Init(&init_info);
}
void ImGuiBackend::begin() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::render(const vk::CommandBuffer &cmd_buf) const {
    TracyVkNamedZone(TracyContext::Vulkan, __tracy_imgui_render_zone, cmd_buf, "ImGui Render", true);
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buf);
}
