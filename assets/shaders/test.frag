#version 450

layout (location = 0) in vec3 in_position_ws;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_tex_coord;

layout (location = 0) out vec4 out_color;

layout (std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 proj;
    vec4 camera;
} scene_uniforms;

layout (set = 1, binding = 0) uniform sampler2D u_tex_albedo;
layout (set = 1, binding = 1) uniform sampler2D u_tex_normal;
layout (set = 1, binding = 2) uniform sampler2D u_tex_omr;
layout (std140, set = 1, binding = 3) uniform MaterialUniforms {
    vec4 albedoFactors;
    vec4 mrnFactors; // metalness, roughness, normal strength
} material_uniforms;

const float PI = 3.14159265359;

const int LIGHT_COUNT = 1;
const vec3 LIGHT_DIRECTION = normalize(vec3(0.5, 1.5, 1));
const vec3 LIGHT_RADIANCE = vec3(15.0);

vec3 transformNormal(mat3 tbn, vec3 tangent_normal) {
    return normalize(tbn * tangent_normal);
}

// Based on https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v32.pdf page 92
float adjustRoughness(vec3 tangent_normal, float roughness) {
    float r = length(tangent_normal);
    if (r < 1.0) {
        float kappa = (3.0 * r - r * r * r) / (1.0 - r * r);
        float variance = 1.0 / kappa;
        // Why is it ok for the roughness to be > 1 ?
        return sqrt(roughness * roughness + variance);
    }
    return roughness;
}


float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a_2 = a * a;
    float n_dot_h = max(dot(N, H), 0.0);
    float n_dot_h_2 = n_dot_h * n_dot_h;

    float nom = a_2;
    float denom = (n_dot_h_2 * (a_2 - 1.0) + 1.0);
    // when roughness is zero and N = H denom would be 0
    denom = PI * denom * denom + 5e-6;

    return nom / denom;
}

float geometrySchlickGGX(float n_dot_v, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = n_dot_v;
    float denom = n_dot_v * (1.0 - k) + k;

    return nom / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    // + 5e-6 to prevent artifacts, value is from https://google.github.io/filament/Filament.html#materialsystem/specularbrdf:~:text=float%20NoV%20%3D%20abs(dot(n%2C%20v))%20%2B%201e%2D5%3B
    float n_dot_v = max(dot(N, V), 0.0) + 5e-6;
    float n_dot_l = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(n_dot_v, roughness);
    float ggx1 = geometrySchlickGGX(n_dot_l, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cos_theta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// https://advances.realtimerendering.com/other/2016/naughty_dog/index.html
float microShadowNaughtyDog(float ao, float n_dot_l) {
    float aperture = 2.0 * ao; // They use ao^2, but linear looks better imo
    return clamp(n_dot_l + aperture - 1.0, 0.0, 1.0);
}

void main() {
    vec4 albedo = texture(u_tex_albedo, in_tex_coord);
    albedo *= material_uniforms.albedoFactors;

    if (albedo.a < 0.5) {
        discard;
    }

    vec3 omr = texture(u_tex_omr, in_tex_coord).xyz;
    omr.yz *= material_uniforms.mrnFactors.xy;
    float occusion = omr.x;
    float metallic = omr.y;
    float roughness = omr.z;
    // I'm not sure if the normalize is required.
    // When passing just the normal vector from VS to FS it is generally required to normalize it again.
    // See https://www.lighthouse3d.com/tutorials/glsl-12-tutorial/normalization-issues/
    // mat3 tbn = mat3(normalize(in_tbn[0]), normalize(in_tbn[1]), normalize(in_tbn[2]));
    mat3 tbn = in_tbn;

    vec3 tN;
    tN.xy = texture(u_tex_normal, in_tex_coord).xy * 2.0 - 1.0;
    tN.z = sqrt(1 - tN.x * tN.x - tN.y * tN.y);
    tN = normalize(tN * vec3(material_uniforms.mrnFactors.z * .5, material_uniforms.mrnFactors.z * .5, 1.0)); // increase intensity
    roughness = adjustRoughness(tN, roughness);

    vec3 N = transformNormal(tbn, tN);
    vec3 P = in_position_ws;
    vec3 V = normalize(scene_uniforms.camera.xyz - P);
    vec3 R = reflect(-V, N);
    float n_dot_v = max(dot(N, V), 0.0);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < LIGHT_COUNT; ++i)
    {
        vec3 L = LIGHT_DIRECTION;
        vec3 radiance = LIGHT_RADIANCE;

        // The half way vector
        vec3 H = normalize(V + L);

        // Use geo normal for surface facing away from light
        vec3 n = mix(N, tbn[2].xyz, clamp(-10 * dot(tbn[2].xyz, L), -0.5, 0.5) + 0.5);

        // Cook-Torrance BRDF
        float NDF = distributionGGX(n, H, roughness);
        float G = geometrySmith(n, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float n_dot_l = max(dot(n, L), 0.0);

        float micro_shadow = microShadowNaughtyDog(occusion, n_dot_l);
        radiance *= micro_shadow;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * n_dot_v * n_dot_l + 1e-5; // + 1e-5 to prevent divide by zero
        vec3 specular = numerator / denominator;

        // kS is equal to Fresnel
        vec3 kS = F;
        // for energy conservation, the diffuse and specular light can't
        // be above 1.0 (unless the surface emits light); to preserve this
        // relationship the diffuse component (kD) should equal 1.0 - kS.
        vec3 kD = vec3(1.0) - kS;
        // multiply kD by the inverse metalness such that only non-metals
        // have diffuse lighting, or a linear blend if partly metal (pure metals
        // have no diffuse light).
        kD *= 1.0 - metallic;

        // add to outgoing radiance Lo
        // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
        Lo += (kD * albedo.rgb / PI + specular) * radiance * n_dot_l;
    }

    vec3 ambient = vec3(1.0);
    ambient *= fresnelSchlickRoughness(n_dot_v, F0, roughness);
    ambient *= albedo.rgb;

    vec3 color = ambient + Lo;
    // reinhard tonemap
    color = color / (1 + color);
    // no gamma correction, swapchain uses srgb format
    out_color = vec4(color, 1.0);
}