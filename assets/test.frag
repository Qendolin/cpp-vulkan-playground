#version 450

layout (location = 0) in vec3 in_color;
layout (location = 1) in vec2 in_tex_coord;

layout (location = 0) out vec4 out_color;

layout (binding = 1) uniform sampler2D u_tex_sampler;

void main() {
    out_color = texture(u_tex_sampler, in_tex_coord);
    // out_color = mix(out_color, vec4(in_tex_coord, 0, 1), 0.5);
}