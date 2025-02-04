#pragma once
#include <vector>
#include <array>
#include <glm/fwd.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <vulkan/vulkan_enums.hpp>

namespace std::filesystem {
    class path;
}

struct PlainImageData;
class GraphicsBackend;

namespace gltf {
    struct Vertex {
        alignas(8) glm::vec3 pos;
        alignas(8) glm::vec3 normal;
        alignas(8) glm::vec2 texCoord;

        static constexpr auto bindingDescriptors() {
            constexpr std::array desc{
                vk::VertexInputBindingDescription2EXT{
                    .binding = 0,
                    .stride = sizeof(Vertex::pos),
                    .inputRate = vk::VertexInputRate::eVertex,
                    .divisor = 1
                },
                vk::VertexInputBindingDescription2EXT{
                    .binding = 1,
                    .stride = sizeof(Vertex::normal),
                    .inputRate = vk::VertexInputRate::eVertex,
                    .divisor = 1
                },
                vk::VertexInputBindingDescription2EXT{
                    .binding = 2,
                    .stride = sizeof(Vertex::texCoord),
                    .inputRate = vk::VertexInputRate::eVertex,
                    .divisor = 1
                }
            };
            return desc;
        }

        static constexpr auto attributeDescriptors() {
            // no offsets because they are not interleaved
            constexpr std::array desc{
                vk::VertexInputAttributeDescription2EXT{
                    .location = 0,
                    .binding = 0,
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = 0,
                },
                vk::VertexInputAttributeDescription2EXT{
                    .location = 1,
                    .binding = 1,
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = 0,
                },
                vk::VertexInputAttributeDescription2EXT{
                    .location = 2,
                    .binding = 2,
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = 0,
                }
            };
            return desc;
        }
    };

    struct Material {
        uint32_t index;
        int albedo;
        int orm;
        int normal;
    };

    struct Instance {
        uint32_t indexOffset;
        uint32_t indexCount;
        int32_t vertexOffset;
        glm::mat4 transformation;
        Material material;
    };

    struct SceneData {
        size_t index_count;
        size_t vertex_count;

        std::vector<unsigned char> vertex_position_data;
        std::vector<unsigned char> vertex_normal_data;
        std::vector<unsigned char> vertex_texcoord_data;
        std::vector<unsigned char> index_data;
        std::vector<PlainImageData> images;
        std::vector<Material> materials;

        std::vector<Instance> instances;
    };

    SceneData load(const std::filesystem::path &path);
}
