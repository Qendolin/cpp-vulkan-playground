#pragma once

#include <memory>


class AppContext;
class ShaderLoader;
class Shader;
class Camera;
namespace glfw {
    class Input;
}

class Application {
    AppContext &ctx;

    glfw::Input &input_;

    std::unique_ptr<ShaderLoader> shaderLoader_;
    std::unique_ptr<Shader> shader_;

    void loadShader();

    void updateInput(Camera &camera);

public:
    Application(AppContext &ctx);

    ~Application();

    void run();
};
