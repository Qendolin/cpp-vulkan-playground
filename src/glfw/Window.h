#pragma once

#include <string>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

namespace glfw {
    enum class ClientApi : int {
        OpenGL = GLFW_OPENGL_API,
        OpenGLES = GLFW_OPENGL_ES_API,
        None = GLFW_NO_API
    };

    enum class ContextCreationApi : int {
        Native = GLFW_NATIVE_CONTEXT_API,
        EGL = GLFW_EGL_CONTEXT_API,
        OSMesa = GLFW_OSMESA_CONTEXT_API
    };

    enum class ContextRobustness : int {
        None = GLFW_NO_ROBUSTNESS,
        NoResetNotification = GLFW_NO_RESET_NOTIFICATION,
        LoseContextOnReset = GLFW_LOSE_CONTEXT_ON_RESET
    };

    enum class ContextReleaseBehavior : int {
        Any = GLFW_ANY_RELEASE_BEHAVIOR,
        Flush = GLFW_RELEASE_BEHAVIOR_FLUSH,
        None = GLFW_RELEASE_BEHAVIOR_NONE,
    };

    struct WindowCreateInfo {
        int width;
        int height;
        std::string title;
        bool resizable = true;
        bool visible = true;
        bool decorated = true;
        bool focused = true;
        bool autoIconify = true;
        bool floating = false;
        bool maximized = false;
        bool centerCursor = true;
        bool transparentFramebuffer = false;
        bool focusOnShow = true;
        bool scaleToMonitor = false;
        bool scaleFramebuffer = true;
        bool mousePassthrough = false;
        int positionX = static_cast<int>(GLFW_ANY_POSITION);
        int positionY = static_cast<int>(GLFW_ANY_POSITION);
        int redBits = 8;
        int greenBits = 8;
        int blueBits = 8;
        int alphaBits = 8;
        int depthBits = 24;
        int stencilBits = 8;
        int samples = 0;
        int refreshRate = GLFW_DONT_CARE;
        bool stereo = false;
        bool srgbCapable = false;
        bool doublebuffer = true;
        ClientApi clientApi = ClientApi::OpenGL;
        ContextCreationApi contextCreationApi = ContextCreationApi::Native;
        int contextVersionMajor = 1;
        int contextVersionMinor = 0;
        ContextRobustness contextRobustness = ContextRobustness::None;
        ContextReleaseBehavior contextReleaseBehavior = ContextReleaseBehavior::Any;
        bool contextDebug = false;
    };

    class Window {
        GLFWwindow* handle = nullptr;
    public:
        explicit Window(const WindowCreateInfo& create_info, GLFWmonitor *monitor = nullptr, GLFWwindow *share = nullptr);
        ~Window();

        [[nodiscard]] bool shouldClose() const;

        [[nodiscard]] vk::Extent2D getFramebufferSize() const;

        vk::UniqueSurfaceKHR createWindowSurfaceKHRUnique(vk::Instance instance);

        operator GLFWwindow*() const { // NOLINT(*-explicit-constructor)
            return handle;
        }
    };
}
