#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in float shade;
layout(location = 3) in float skyLight;
layout(location = 4) in vec4 tint;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vLight;
layout(location = 2) out float vDistance;
layout(location = 3) out vec4 vTint;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
    // x: 1 when clip-space depth runs from 0 to 1
    vec4 extra;
} frame;

void main()
{
    vTexCoord = texCoord;
    float light = max(skyLight * frame.params.z, frame.params.w);
    vLight = shade * light;
    vTint = tint;
    vDistance = length(position - frame.cameraPosition.xyz);
    vec4 clip = frame.viewProjection * vec4(position, 1.0);
    // Squeeze the depth into the front 5 % of the range so the hand is drawn over the world.
    float depth = clip.z / clip.w;
    float front = frame.extra.x > 0.5 ? depth : depth * 0.5 + 0.5;
    front *= 0.05;
    clip.z = (frame.extra.x > 0.5 ? front : front * 2.0 - 1.0) * clip.w;
    gl_Position = clip;
}
