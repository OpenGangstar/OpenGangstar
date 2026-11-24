#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 tint;
layout(location = 3) in vec3 worldNormal;
layout(location = 4) in vec2 auxiliaryUv;
layout(location = 5) in vec2 reflectionUv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D diffuseTexture;
layout(set = 0, binding = 1) uniform sampler2D secondTexture;
layout(set = 0, binding = 2) uniform TextureMatrices {
    vec4 firstU;
    vec4 firstV;
    vec4 secondU;
    vec4 secondV;
} textureMatrices;
layout(push_constant) uniform Material {
    layout(offset = 64) vec4 tintAndCutoff;
    float brightness;
    uint materialMode;
    float diffuseAlpha;
    uint stageState;
} material;

vec4 combineMaterial(vec4 diffuse, vec4 primary)
{
    float alpha = material.materialMode == 14u || material.materialMode == 15u
        ? diffuse.a : diffuse.a * primary.a;
    vec4 result = vec4(diffuse.rgb * primary.rgb, alpha);
    if (material.materialMode != 3u && material.materialMode != 12u && material.materialMode != 17u)
        return result;
    if (material.materialMode == 3u) {
        // REPLACE differs for original RGB and RGBA uploads, and a disabled stage passes P.
        result = (material.stageState & 8u) != 0u
            ? vec4(diffuse.rgb, (material.stageState & 1u) != 0u ? primary.a : diffuse.a)
            : primary;
    }
    result = clamp(result, 0.0, 1.0);
    if ((material.stageState & 2u) == 0u) return result;
    vec3 coordinates = vec3(material.materialMode == 3u ? auxiliaryUv : reflectionUv, 1.0);
    vec2 secondUv = vec2(dot(textureMatrices.secondU.xyz, coordinates),
                         dot(textureMatrices.secondV.xyz, coordinates));
    vec4 second = texture(secondTexture, secondUv);
    if (material.materialMode == 3u)
        return vec4(result.rgb * second.rgb, result.a * result.a);
    float combinedAlpha = (material.stageState & 4u) != 0u ? result.a * result.a : result.a * second.a;
    return vec4(clamp(result.rgb + second.rgb, 0.0, 1.0), combinedAlpha);
}

void main()
{
    vec3 coordinates = vec3(uv, 1.0);
    vec2 firstUv = vec2(dot(textureMatrices.firstU.xyz, coordinates), dot(textureMatrices.firstV.xyz, coordinates));
    vec4 diffuse = texture(diffuseTexture, firstUv);
    if (material.tintAndCutoff.a == -3.0 && uv.x < material.tintAndCutoff.x) discard;
    if ((material.stageState & 32u) != 0u) {
        // Original setupArrays chooses literal current RGBA OR primary color0.
        // CommonGL disables GL_LIGHTING even when its material flag2 is requested.
        vec4 primary = (material.stageState & 16u) != 0u
            ? vec4(material.tintAndCutoff.rgb, material.diffuseAlpha) : tint;
        primary.rgb *= material.brightness;
        if (material.tintAndCutoff.a > 0.0 && diffuse.a <= material.tintAndCutoff.a) discard;
        color = combineMaterial(diffuse, primary);
        return;
    }
    // Independent UI and particle producers retain their existing primary values.
    float primaryAlpha = tint.a * material.diffuseAlpha;
    if (material.tintAndCutoff.a == -2.0) {
        color = combineMaterial(diffuse, vec4(material.tintAndCutoff.rgb * material.brightness * tint.rgb, primaryAlpha));
        return;
    }
    if (material.tintAndCutoff.a < 0.0) {
        color = combineMaterial(diffuse, vec4(material.brightness * tint.rgb, primaryAlpha));
        return;
    }
    // The retail alpha-reference material binds stage zero and uses its texture alpha.
    if (material.tintAndCutoff.a > 0.0 && diffuse.a <= material.tintAndCutoff.a) discard;
    color = combineMaterial(diffuse, vec4(material.tintAndCutoff.rgb * tint.rgb * material.brightness, primaryAlpha));
}
