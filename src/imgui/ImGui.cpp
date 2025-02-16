#include "ImGui.h"

#include "../GraphicsBackend.h"
#include "../Swapchain.h"

void initImGui(const AppContext& ctx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(ctx.window.get()), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = ctx.device.instace.get();
    init_info.PhysicalDevice = ctx.device.physicalDevice;
    init_info.Device = ctx.device.get();
    init_info.QueueFamily = ctx.device.mainQueueFamily;
    init_info.Queue = ctx.device.mainQueue;
    init_info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 512;
    init_info.MinImageCount = ctx.swapchain->minImageCount();
    init_info.ImageCount = ctx.swapchain->imageCount();
    init_info.UseDynamicRendering = true;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = [](VkResult result) { vk::detail::resultCheck(static_cast<vk::Result>(result), "ImGui"); };
    vk::Format color_attachment_format = ctx.swapchain->colorFormatLinear();
    init_info.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_attachment_format,
        .depthAttachmentFormat = ctx.swapchain->depthFormat(),
    };

    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_Init(&init_info);
}
