#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_tex_coord;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec2 out_tex_coord;

layout (binding = 0) uniform UniformBufferObject {
    mat4 model; // deprecated
    mat4 view;
    mat4 proj;
} ubo;

layout (push_constant) uniform constants
{
    mat4 model;
} PushConstants;

void main() {
    gl_Position = ubo.proj * ubo.view * PushConstants.model * vec4(in_position, 1.0);
    out_normal = in_normal;
    out_tex_coord = in_tex_coord;
}