#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 vertexNormal;
layout(location = 4) in vec2 secondaryTexCoord;
layout(location = 5) in vec4 rawReflectionNormal;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec4 tint;
layout(location = 3) out vec3 worldNormal;
layout(location = 4) out vec2 auxiliaryUv;
layout(location = 5) out vec2 reflectionUv;
layout(push_constant) uniform Camera {
    vec4 x;
    vec4 y;
    vec4 z;
    vec4 w;
    layout(offset = 96) vec4 reflectionViewX;
    vec4 reflectionViewY;
} camera;

void main()
{
    vec4 world = vec4(position, 1.0);
    gl_Position = vec4(dot(camera.x, world), dot(camera.y, world), dot(camera.z, world), dot(camera.w, world));
    worldPosition = position;
    uv = texCoord;
    tint = vertexColor;
    worldNormal = vertexNormal;
    auxiliaryUv = secondaryTexCoord;
    // Original softTexGen mode2: raw view*world linear normal, normalize=false.
    reflectionUv = rawReflectionNormal.w != 0.0 ? vec2(
        dot(camera.reflectionViewX.xyz, rawReflectionNormal.xyz),
        dot(camera.reflectionViewY.xyz, rawReflectionNormal.xyz)) * 0.5 + 0.5 : secondaryTexCoord;
}
