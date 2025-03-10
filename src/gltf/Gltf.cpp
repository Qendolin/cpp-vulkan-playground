#include "Gltf.h"

#include <glm/fwd.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

#include "../GraphicsBackend.h"
#include "../Image.h"
#include "../Logger.h"

namespace gltf {
    template<typename D>
    std::span<D> cast_span(const std::span<uint8_t> &byte_span) {
        return std::span(reinterpret_cast<D *>(byte_span.data()), byte_span.size() / sizeof(D));
    }

    template<typename D>
    std::span<D> cast_span(const std::span<const uint8_t> &byte_span) {
        return std::span(reinterpret_cast<D *>(byte_span.data()), byte_span.size() / sizeof(D));
    }

    // loads the node's transformation information into a single glm::mat4
    glm::mat4 loadNodeTransform(const tinygltf::Node &node) {
        auto transform = glm::mat4(1.0);
        // A Node can have either a full transformation matrix or individual scale, rotation and translatin components

        // Check if matrix is present
        if (node.matrix.size() == 16) {
            transform = glm::make_mat4(node.matrix.data());
        } else {
            // Check if scale is present
            if (node.scale.size() == 3) {
                transform = glm::scale(transform, {node.scale[0], node.scale[1], node.scale[2]});
            }
            // Check if rotation is present
            if (node.rotation.size() == 4) {
                glm::quat quat = glm::make_quat(node.rotation.data());
                transform = glm::mat4_cast(quat) * transform;
            }
            // Check if translation is present
            if (node.translation.size() == 3) {
                glm::mat4 mat =
                        glm::translate(glm::mat4(1.0), {node.translation[0], node.translation[1], node.translation[2]});
                transform = mat * transform;
            }
        }
        return transform;
    }

    struct PrimitiveInfo {
        uint32_t indexOffset;
        uint32_t indexCount;
        int32_t vertexOffset;
    };

    void loadMeshes(
            const tinygltf::Model &model,
            std::vector<uint8_t> &vertex_positions,
            std::vector<uint8_t> &vertex_normals,
            std::vector<uint8_t> &vertex_tangents,
            std::vector<uint8_t> &vertex_texcoords,
            std::vector<uint8_t> &vertex_indices,
            std::vector<PrimitiveInfo> &primitive_infos,
            std::vector<uint32_t> &mesh_primitive_indices
    ) {
        using namespace tinygltf;

        size_t index_index = 0;
        size_t counted_verts = 0;

        auto vertex_indices_view = cast_span<uint32_t>(std::span(vertex_indices));

        for (size_t i = 0; i < model.meshes.size(); i++) {
            const auto &mesh = model.meshes[i];
            mesh_primitive_indices[i] = static_cast<uint32_t>(primitive_infos.size());

            for (const auto &prim: mesh.primitives) {
                Logger::check(
                        prim.mode == TINYGLTF_MODE_TRIANGLES, "Unsupported primitive mode: " + std::to_string(prim.mode)
                );

                int position_access_i = -1;
                int normal_access_i = -1;
                int tangent_access_i = -1;
                int texcoord_access_i = -1;

                for (auto &[key, j]: prim.attributes) {
                    if (key == "POSITION") {
                        position_access_i = j;
                    } else if (key == "NORMAL") {
                        normal_access_i = j;
                    } else if (key == "TANGENT") {
                        tangent_access_i = j;
                    } else if (key == "TEXCOORD_0") {
                        texcoord_access_i = j;
                    }
                }

                const Accessor &position_access = model.accessors[position_access_i];
                const Accessor &normal_access = model.accessors[normal_access_i];
                const Accessor &tangent_access = model.accessors[tangent_access_i];
                const Accessor &texcoord_access = model.accessors[texcoord_access_i];
                const Accessor &index_access = model.accessors[prim.indices];

                Logger::check(position_access.byteOffset == 0, "Position accessor byte offset must be 0");
                Logger::check(normal_access.byteOffset == 0, "Normal accessor byte offset must be 0");
                Logger::check(tangent_access.byteOffset == 0, "Tangent accessor byte offset must be 0");
                Logger::check(texcoord_access.byteOffset == 0, "Texcoord accessor byte offset must be 0");
                Logger::check(index_access.byteOffset == 0, "Index accessor byte offset must be 0");

                const BufferView &position_view = model.bufferViews[position_access.bufferView];
                const BufferView &normal_view = model.bufferViews[normal_access.bufferView];
                const BufferView &tangent_view = model.bufferViews[tangent_access.bufferView];
                const BufferView &texcoord_view = model.bufferViews[texcoord_access.bufferView];
                const BufferView &index_view = model.bufferViews[index_access.bufferView];

                Logger::check(position_view.byteStride == 0, "Position buffer must be tightly packed");
                Logger::check(normal_view.byteStride == 0, "Normal buffer must be tightly packed");
                Logger::check(tangent_view.byteStride == 0, "Tangent buffer must be tightly packed");
                Logger::check(texcoord_view.byteStride == 0, "Texcoord buffer must be tightly packed");
                Logger::check(index_view.byteStride == 0, "Index buffer must be tightly packed");

                const Buffer &position_buffer = model.buffers[position_view.buffer];
                const Buffer &normal_buffer = model.buffers[normal_view.buffer];
                const Buffer &tangent_buffer = model.buffers[tangent_view.buffer];
                const Buffer &texcoord_buffer = model.buffers[texcoord_view.buffer];
                const Buffer &index_buffer = model.buffers[index_view.buffer];

                const auto position_span = std::span(
                        position_buffer.data.cbegin() + static_cast<ptrdiff_t>(position_view.byteOffset), position_view.byteLength
                );
                const auto normal_span = std::span(
                        normal_buffer.data.cbegin() + static_cast<ptrdiff_t>(normal_view.byteOffset), normal_view.byteLength
                );
                const auto tangent_span = std::span(
                        tangent_buffer.data.cbegin() + static_cast<ptrdiff_t>(tangent_view.byteOffset), tangent_view.byteLength
                );
                const auto texcoord_span = std::span(
                        texcoord_buffer.data.cbegin() + static_cast<ptrdiff_t>(texcoord_view.byteOffset), texcoord_view.byteLength
                );
                vertex_positions.insert(vertex_positions.end(), position_span.begin(), position_span.end());
                vertex_normals.insert(vertex_normals.end(), normal_span.begin(), normal_span.end());
                vertex_tangents.insert(vertex_tangents.end(), tangent_span.begin(), tangent_span.end());
                vertex_texcoords.insert(vertex_texcoords.end(), texcoord_span.begin(), texcoord_span.end());

                primitive_infos.emplace_back() = {
                    .indexOffset = static_cast<uint32_t>(index_index),
                    .indexCount = static_cast<uint32_t>(index_access.count),
                    .vertexOffset = static_cast<int32_t>(counted_verts),
                };

                const auto index_span = std::span(
                        index_buffer.data.cbegin() + static_cast<ptrdiff_t>(index_view.byteOffset), index_view.byteLength
                );

                if (index_access.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    Logger::check(
                            reinterpret_cast<std::uintptr_t>(index_span.data()) % alignof(uint16_t) == 0,
                            "Index data is not aligned to uint16"
                    );
                    auto indices_as_shorts = cast_span<const uint16_t>(index_span);
                    for (uint16_t index_short: indices_as_shorts) {
                        vertex_indices_view[index_index++] = static_cast<uint32_t>(index_short);
                    }
                } else if (index_access.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    auto indices_as_ints = cast_span<const uint32_t>(index_span);
                    for (uint32_t index_int: indices_as_ints) {
                        vertex_indices_view[index_index++] = index_int;
                    }
                } else {
                    Logger::check(false, "Index component type must be unsigned short or int");
                }
                counted_verts += position_access.count;
            }
        }
    }

    SceneData load(const std::filesystem::path &path) {
        using namespace tinygltf;

        Model model;
        TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());

        if (!warn.empty()) {
            Logger::warning("GLTF: " + warn);
        }

        if (!err.empty()) {
            Logger::error("GLTF: " + err);
        }

        if (!ret) {
            Logger::panic("failed to load GLTF");
        }


        Scene &scene = model.scenes[model.defaultScene];

        size_t index_count = 0;
        size_t vertex_count = 0;
        for (auto node_i: scene.nodes) {
            for (const auto &prim: model.meshes[model.nodes[node_i].mesh].primitives) {
                index_count += model.accessors[prim.indices].count;
                vertex_count += model.accessors[prim.attributes.at("POSITION")].count;
            }
        }

        SceneData scene_data = {};
        scene_data.vertex_position_data.reserve(vertex_count * sizeof(Vertex::pos));
        scene_data.vertex_normal_data.reserve(vertex_count * sizeof(Vertex::normal));
        scene_data.vertex_tangent_data.reserve(vertex_count * sizeof(Vertex::tangent));
        scene_data.vertex_texcoord_data.reserve(vertex_count * sizeof(Vertex::texCoord));
        scene_data.index_data.resize(index_count * sizeof(uint32_t));

        std::vector<PrimitiveInfo> primitive_infos;
        std::vector<uint32_t> mesh_primitive_indices(model.meshes.size());

        loadMeshes(
                model, scene_data.vertex_position_data, scene_data.vertex_normal_data, scene_data.vertex_tangent_data,
                scene_data.vertex_texcoord_data, scene_data.index_data, primitive_infos, mesh_primitive_indices
        );

        scene_data.images.resize(model.textures.size());
        auto load_texture = [&scene_data, &model](int texture_index, int image_index, vk::Format format) {
            if (scene_data.images[texture_index]) {
                Logger::check(scene_data.images[texture_index].format == format, "Image loaded in different formats");
            }

            const auto &image = model.images[image_index];
            Logger::check(image.bits == 8, "Only 8-bit images are supported");
            scene_data.images[texture_index] =
                    PlainImageData::create(format, image.width, image.height, image.component, image.image.data());
        };

        for (const auto &material: model.materials) {
            Material &mat = scene_data.materials.emplace_back();
            mat.index = static_cast<uint32_t>(scene_data.materials.size()) - 1;
            mat.albedoFactor = glm::vec4(
                    material.pbrMetallicRoughness.baseColorFactor.at(0),
                    material.pbrMetallicRoughness.baseColorFactor.at(1),
                    material.pbrMetallicRoughness.baseColorFactor.at(2),
                    material.pbrMetallicRoughness.baseColorFactor.at(3)
            );
            mat.metaillicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
            mat.roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
            mat.normalFactor = static_cast<float>(material.normalTexture.scale);
            int albedo_index = material.pbrMetallicRoughness.baseColorTexture.index;
            if (albedo_index != -1) {
                const auto &albedo_texture = model.textures[albedo_index];
                load_texture(albedo_index, albedo_texture.source, vk::Format::eR8G8B8A8Srgb);
                mat.albedo = albedo_index;
            }
            int o_index = material.occlusionTexture.index;
            PlainImageData *omr_image_data = nullptr;
            if (o_index != -1) {
                const auto &o_texture = model.textures[o_index];
                load_texture(o_index, o_texture.source, vk::Format::eR8G8B8A8Unorm);
                omr_image_data = &scene_data.images[o_index];
                mat.omr = o_index;
            }
            int mr_index = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
            if (mr_index != -1) {
                const auto &mr_texture = model.textures[mr_index];
                const auto &mr_image = model.images[mr_texture.source];
                // TODO: This is untested, and I think its wrong
                if (omr_image_data) {
                    Logger::check(
                            mr_image.width == omr_image_data->width && mr_image.height == omr_image_data->height,
                            "Occlusion texture size doesn't match metalness-roughness texture size"
                    );
                    PlainImageData mr_data = PlainImageData::create(
                            vk::Format::eR8G8Unorm, mr_image.width, mr_image.height, mr_image.component,
                            mr_image.image.data()
                    );
                    mr_data.copyChannels(*omr_image_data, {-1, 1, 2});
                } else {
                    load_texture(mr_index, mr_texture.source, vk::Format::eR8G8B8A8Unorm);
                    mat.omr = mr_index;
                }
            }
            if (o_index != -1 && mr_index == -1 && omr_image_data) {
                omr_image_data->fill({1, 2}, {0xff, 0xff});
            }
            int normal_index = material.normalTexture.index;
            if (normal_index != -1) {
                const auto &normal_texture = model.textures[normal_index];
                load_texture(normal_index, normal_texture.source, vk::Format::eR8G8Unorm);
                mat.normal = normal_index;
            }
        }

        for (const int node_i: scene.nodes) {
            Node &node = model.nodes[node_i];
            Mesh &mesh = model.meshes[node.mesh];
            for (size_t i = 0; i < mesh.primitives.size(); i++) {
                const auto &prim = mesh.primitives[i];
                const auto &prim_data = primitive_infos[mesh_primitive_indices[node.mesh] + i];
                const auto &material = scene_data.materials[prim.material];
                scene_data.instances.emplace_back() = {
                    .indexOffset = static_cast<uint32_t>(prim_data.indexOffset),
                    .indexCount = static_cast<uint32_t>(prim_data.indexCount),
                    .vertexOffset = static_cast<int32_t>(prim_data.vertexOffset),
                    .transformation = loadNodeTransform(node),
                    .material = material
                };
            }
        }

        scene_data.index_count = scene_data.index_data.size() / sizeof(uint32_t);
        scene_data.vertex_count = scene_data.vertex_position_data.size() / sizeof(Vertex::pos);

        return scene_data;
    }
} // namespace gltf
