#pragma once

#include <string>
#include <vulkan/vulkan.hpp>

struct GLFWwindow;
struct GLFWmonitor;

namespace glfw {
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
        int positionX = static_cast<int>(0x80000000);
        int positionY = static_cast<int>(0x80000000);
        int redBits = 8;
        int greenBits = 8;
        int blueBits = 8;
        int alphaBits = 8;
        int depthBits = 24;
        int stencilBits = 8;
        int samples = 0;
        int refreshRate = -1;
        bool stereo = false;
        bool srgbCapable = false;
        bool doublebuffer = true;
    };

    class Window {
        GLFWwindow *handle = nullptr;

    public:
        explicit Window(const WindowCreateInfo &create_info, GLFWmonitor *monitor = nullptr, GLFWwindow *share = nullptr);

        explicit Window(GLFWwindow *handle) : handle(handle) {}

        Window() = default;

        Window(const Window &other) = default;

        Window &operator=(const Window &other) = default;

        [[nodiscard]] bool shouldClose() const;

        [[nodiscard]] vk::Extent2D getFramebufferSize() const;

        [[nodiscard]] vk::UniqueSurfaceKHR createWindowSurfaceKHRUnique(vk::Instance instance) const;

        explicit operator GLFWwindow *() const { return handle; }

        void centerOnScreen() const;
    };

    class UniqueWindow {
        Window window = {};

    public:
        explicit UniqueWindow(const WindowCreateInfo &create_info, GLFWmonitor *monitor = nullptr, GLFWwindow *share = nullptr)
            : window(create_info, monitor, share) {}

        explicit UniqueWindow(GLFWwindow *handle) : window(handle) {}

        UniqueWindow() = default;

        UniqueWindow(const UniqueWindow &) = delete;

        UniqueWindow &operator=(const UniqueWindow &) = delete;

        UniqueWindow(UniqueWindow &&other) noexcept;

        UniqueWindow &operator=(UniqueWindow &&other) noexcept;

        const Window *operator->() const noexcept { return &window; }

        Window *operator->() noexcept { return &window; }

        const Window &operator*() const noexcept { return window; }

        Window &operator*() noexcept { return window; }

        ~UniqueWindow() { reset(); }

        void reset() noexcept;

        [[nodiscard]] Window &get() noexcept { return window; }

        [[nodiscard]] const Window &get() const noexcept { return window; }

        explicit operator GLFWwindow *() const { return static_cast<GLFWwindow *>(window); }
    };
} // namespace glfw
