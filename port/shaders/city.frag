#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 tint;
layout(location = 3) in vec3 worldNormal;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D diffuseTexture;
layout(push_constant) uniform Material {
    layout(offset = 64) vec4 tintAndCutoff;
    float brightness;
} material;

void main()
{
    vec4 diffuse = texture(diffuseTexture, uv);
    if (material.tintAndCutoff.a == -3.0 && uv.x < material.tintAndCutoff.x) discard;
    if (material.tintAndCutoff.a == -2.0) {
        color = vec4(diffuse.rgb * material.tintAndCutoff.rgb * material.brightness, diffuse.a) * tint;
        return;
    }
    if (material.tintAndCutoff.a < 0.0) {
        color = vec4(diffuse.rgb * material.brightness, diffuse.a) * tint;
        return;
    }
    vec3 faceNormal = cross(dFdx(worldPosition), dFdy(worldPosition));
    vec3 surface = dot(worldNormal, worldNormal) > 1e-12 ? normalize(worldNormal) : normalize(faceNormal);
    vec3 light = normalize(vec3(-0.35, -0.45, 1.0));
    float shade = 0.30 + 0.70 * abs(dot(surface, light));
    // The retail alpha-reference material binds stage zero and uses its texture alpha.
    if (material.tintAndCutoff.a > 0.0 && diffuse.a <= material.tintAndCutoff.a) discard;
    color = vec4(diffuse.rgb * material.tintAndCutoff.rgb * tint.rgb * shade * material.brightness, 1.0);
}
