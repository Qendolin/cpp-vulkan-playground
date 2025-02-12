#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec4 in_tangent;
layout (location = 3) in vec2 in_tex_coord;

layout (location = 0) out vec3 out_position_ws;
layout (location = 1) out mat3 out_tbn;
layout (location = 4) out vec2 out_tex_coord;

layout (set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
    vec4 camera;
} scene_uniforms;

layout (push_constant) uniform constants
{
    mat4 model;
} PushConstants;

void main() {
    vec4 position_ws = PushConstants.model * vec4(in_position, 1.0);
    gl_Position = scene_uniforms.proj * scene_uniforms.view * position_ws;
    out_position_ws = position_ws.xyz;
    out_tex_coord = in_tex_coord;

    // Only correct with non uniform scaling
    mat3 normal_matrix = mat3(PushConstants.model);
    vec3 T = normalize(normal_matrix * in_tangent.xyz);
    vec3 N = normalize(normal_matrix * in_normal);
    vec3 bitangent = cross(in_normal, in_tangent.xyz) * in_tangent.w;
    vec3 B = normalize(normal_matrix * bitangent);
    out_tbn = mat3(T, B, N);
}