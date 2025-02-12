#include "ImGui.h"

#include "../GraphicsBackend.h"

void initImGui(const GraphicsBackend &backend, const Swapchain &swapchain) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(backend.window), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = *backend.instance;
    init_info.PhysicalDevice = backend.phyicalDevice;
    init_info.Device = *backend.device;
    init_info.QueueFamily = backend.graphicsQueueIndex;
    init_info.Queue = backend.graphicsQueue;
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 512;
    init_info.MinImageCount = swapchain.minImageCount();
    init_info.ImageCount = swapchain.imageCount();
    init_info.UseDynamicRendering = true;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = [](VkResult result) { vk::detail::resultCheck(vk::Result(result), "ImGui"); };
    vk::Format color_attachment_format = swapchain.colorFormatLinear();
    init_info.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_attachment_format,
        .depthAttachmentFormat = swapchain.depthFormat(),
    };

    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_Init(&init_info);
}
