#pragma once
#include <filesystem>
#include <memory>
#include <vector>

namespace vk {
    enum class ShaderStageFlagBits : uint32_t;
}

namespace shaderc {
    class Compiler;
}

struct ShaderCompileOptions {
    bool optimize = false;
    bool debug = false;
    bool print = false;
};

class ShaderCompiler {
    std::unique_ptr<shaderc::Compiler> compiler;

public:
    ShaderCompiler();

    ~ShaderCompiler();

    [[nodiscard]] std::vector<uint32_t> compile(
            const std::filesystem::path &source_path, vk::ShaderStageFlagBits stage, ShaderCompileOptions opt
    ) const;
};
