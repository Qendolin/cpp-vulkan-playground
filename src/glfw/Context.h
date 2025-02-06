#pragma once
#include <vector>

namespace glfw {
    class Context {
        static inline bool isInitialized = false;

        static void defaultErrorCallback(int error, const char *description);

    public:
        Context();

        Context(const Context &other) = delete;

        Context(Context &&other) noexcept = default;

        Context &operator=(const Context &other) = delete;

        Context &operator=(Context &&other) = delete;

        ~Context();

        [[nodiscard]] static std::vector<const char *> getRequiredInstanceExtensions();
    };
}
