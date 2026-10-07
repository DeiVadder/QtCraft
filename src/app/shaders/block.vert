#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in float shade;
layout(location = 3) in float skyLight;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vLight;
layout(location = 2) out float vDistance;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
} frame;

void main()
{
    vTexCoord = texCoord;
    float light = max(skyLight * frame.params.z, frame.params.w);
    vLight = shade * light;
    vDistance = length(position - frame.cameraPosition.xyz);
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
