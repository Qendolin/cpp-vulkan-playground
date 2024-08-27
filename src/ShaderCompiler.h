#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vk {
    enum class ShaderStageFlagBits : uint32_t;
}

namespace shaderc {
    class Compiler;
}

class ShaderCompiler {
    std::unique_ptr<shaderc::Compiler> compiler;
public:
    ShaderCompiler();
    ~ShaderCompiler();

    std::vector<uint32_t> compile(const std::filesystem::path &source_path, vk::ShaderStageFlagBits stage, bool optimize, bool debug);
};
