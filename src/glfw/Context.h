#pragma once
#include <string>
#include <vector>


namespace glfw {
    class Context {
        static inline bool isInitialized = false;
        static void defaultErrorCallback(int error, const char *description);
    public:
        Context();
        ~Context();

        std::vector<const char *> getRequiredInstanceExtensions() const;

    };
}
