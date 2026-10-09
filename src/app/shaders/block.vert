#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
// x: face shading and ambient occlusion, y: sky light, z: block light (unsigned normalised bytes).
layout(location = 2) in vec4 light;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vLight;
layout(location = 2) out float vDistance;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
} frame;

// Block light (torches) is warm and does not follow the time of day.
const vec3 blockTint = vec3(1.0, 0.86, 0.66);

void main()
{
    vTexCoord = texCoord;
    float sky = max(light.y * frame.params.z, frame.params.w);
    vLight = light.x * max(vec3(sky), light.z * blockTint);
    vDistance = length(position - frame.cameraPosition.xyz);
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
