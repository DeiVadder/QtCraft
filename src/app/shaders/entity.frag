#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in vec4 vTint;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
} frame;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
    vec4 texel = texture(atlas, vTexCoord) * vTint;
    if (texel.a < 0.1)
        discard;
    vec3 lit = texel.rgb * vLight;
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), texel.a);
}
