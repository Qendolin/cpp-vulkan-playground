#pragma once

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace vk {
    class CommandBuffer;
}
class AppContext;
class Swapchain;
class GraphicsBackend;

namespace glfw {
    class Window;
}

class Swapchain;
class DeviceContext;

class ImGuiBackend {

public:
    ImGuiBackend(const DeviceContext &device, const glfw::Window &window, const Swapchain &swapchain);

    void begin() const;

    void render(const vk::CommandBuffer &cmd_buf) const;
};
