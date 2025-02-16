#pragma once

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>

#include "ImGui.h"


class AppContext;
class Swapchain;
class GraphicsBackend;

namespace glfw {
    class Window;
}

void initImGui(const AppContext& ctx);
