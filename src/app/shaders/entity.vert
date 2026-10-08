#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in float shade;
layout(location = 3) in float skyLight;
layout(location = 4) in vec4 tint;
layout(location = 5) in float blockLight;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vLight;
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
    float sky = max(skyLight * frame.params.z, frame.params.w);
    // Block light (torches) is warm and does not follow the time of day.
    vLight = shade * max(vec3(sky), blockLight * vec3(1.0, 0.86, 0.66));
    vTint = tint;
    vDistance = length(position - frame.cameraPosition.xyz);
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
