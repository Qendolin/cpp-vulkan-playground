#pragma once

#include <memory>

#include "FrameResource.h"
#include "glfw/Context.h"
#include "glfw/Window.h"

class ShaderLoader;
class GraphicsBackend;

class Application {
private:
    std::unique_ptr<GraphicsBackend> backend;
    std::unique_ptr<ShaderLoader> loader;
    FrameResourceManager<2> frameResources;

public:
    Application();

    ~Application();

    void run();
};

