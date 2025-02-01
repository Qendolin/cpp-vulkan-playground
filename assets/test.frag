#version 450

layout (location = 0) in vec3 in_normal;
layout (location = 1) in vec2 in_tex_coord;

layout (location = 0) out vec4 out_color;

layout (set = 1, binding = 0) uniform sampler2D u_tex_albedo;
layout (set = 1, binding = 1) uniform sampler2D u_tex_normal;
layout (set = 1, binding = 2) uniform sampler2D u_tex_orm;

void main() {
    out_color = texture(u_tex_albedo, in_tex_coord);
    //    out_color.rgb = vec3(in_tex_coord, 0.0);
    // out_color = mix(out_color, vec4(in_tex_coord, 0, 1), 0.5);
}