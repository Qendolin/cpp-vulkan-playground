#include "ShaderCompiler.h"

#include <filesystem>
#include <fstream>
#include <shaderc/shaderc.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "Logger.h"


static std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Logger::panic("Error opening file: " + std::filesystem::absolute(path).string());
    }

    file.seekg(0, std::ios::end);
    std::streampos size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content;
    content.resize(size);
    file.read(content.data(), size);
    file.close();

    return content;
}

class ShaderIncluder final : public shaderc::CompileOptions::IncluderInterface {
    struct IncludeResult : shaderc_include_result {
        const std::string source_name_str;
        const std::string content_str;

        IncludeResult(std::string source_name, std::string content)
            : shaderc_include_result(), source_name_str(std::move(source_name)), content_str(std::move(content)) {
            this->source_name = source_name_str.data();
            this->source_name_length = source_name_str.size();
            this->content = content_str.data();
            this->content_length = content_str.size();
            this->user_data = nullptr;
        }
    };

    shaderc_include_result *GetInclude(
            const char *requested_source, shaderc_include_type type, const char *requesting_source, size_t
    ) override {
        std::filesystem::path file_path;
        if (type == shaderc_include_type_relative) {
            file_path = std::filesystem::path(requesting_source).parent_path() / requested_source;
            if (!std::filesystem::exists(file_path))
                Logger::panic(
                        "Shader file " + std::string(requested_source) + " loaded from " +
                        std::string(requesting_source) + " does not exist"
                );
        } else {
            file_path = std::filesystem::path(requested_source);
        }

        std::string file_path_string = file_path.string();
        std::string content = read_file(file_path_string);
        return new IncludeResult(file_path_string, content);
    }

    void ReleaseInclude(shaderc_include_result *data) override { delete static_cast<IncludeResult *>(data); }
};

ShaderCompiler::ShaderCompiler() { compiler = std::make_unique<shaderc::Compiler>(); }

ShaderCompiler::~ShaderCompiler() = default;

std::vector<uint32_t> ShaderCompiler::compile(
        const std::filesystem::path &source_path, vk::ShaderStageFlagBits stage, ShaderCompileOptions opt
) const {
    shaderc::CompileOptions options = {};

    if (opt.debug)
        options.SetGenerateDebugInfo();

    options.SetIncluder(std::make_unique<ShaderIncluder>());

    std::string source = read_file(source_path);

    shaderc_shader_kind kind;
    switch (stage) {
        case vk::ShaderStageFlagBits::eVertex:
            kind = shaderc_vertex_shader;
            break;
        case vk::ShaderStageFlagBits::eTessellationControl:
            kind = shaderc_tess_control_shader;
            break;
        case vk::ShaderStageFlagBits::eTessellationEvaluation:
            kind = shaderc_tess_evaluation_shader;
            break;
        case vk::ShaderStageFlagBits::eGeometry:
            kind = shaderc_geometry_shader;
            break;
        case vk::ShaderStageFlagBits::eFragment:
            kind = shaderc_fragment_shader;
            break;
        case vk::ShaderStageFlagBits::eCompute:
            kind = shaderc_compute_shader;
            break;
        default:
            Logger::panic("Unknown shader type: " + source_path.string());
    }

    shaderc::PreprocessedSourceCompilationResult preprocessed_result =
            compiler->PreprocessGlsl(source, kind, source_path.string().c_str(), options);

    if (preprocessed_result.GetCompilationStatus() != shaderc_compilation_status_success) {
        Logger::panic(preprocessed_result.GetErrorMessage());
    }

    std::string preprocessed_code = {preprocessed_result.cbegin(), preprocessed_result.cend()};

    if (opt.print)
        Logger::info("Preprocessed source of " + source_path.string() + ": \n" + preprocessed_code);

    if (opt.optimize)
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult module =
            compiler->CompileGlslToSpv(preprocessed_code, kind, source_path.string().c_str(), options);

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        Logger::panic("Shader compilation failed:\n" + module.GetErrorMessage());
    }

    return {module.begin(), module.end()};
}
