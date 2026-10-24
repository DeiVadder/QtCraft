#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in float shade;
layout(location = 3) in float skyLight;
layout(location = 4) in vec4 tint;
layout(location = 5) in float blockLight;
// Per draw (QcDrawOrigins, decision E-883): xyz the mesh's origin relative to the camera; the
// vertices are relative to the origin (QcEntityMesh::origin).
layout(location = 6) in vec4 origin;

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
    // x: 1 when clip-space depth runs from 0 to 1, w: floor light per unit of sky light
    vec4 extra;
} frame;

void main()
{
    vTexCoord = texCoord;
    // The floor rises with the sky light (decision E-723).
    float sky = max(skyLight * frame.params.z, frame.params.w + skyLight * frame.extra.w);
    // Block light (torches) is warm and does not follow the time of day.
    vLight = shade * max(vec3(sky), blockLight * vec3(1.0, 0.86, 0.66));
    vTint = tint;
    vec3 relative = position + origin.xyz;
    vDistance = length(relative - frame.cameraPosition.xyz);
    vec4 clip = frame.viewProjection * vec4(relative, 1.0);
    // Squeeze the depth into the front 5 % of the range so the hand is drawn over the world.
    float depth = clip.z / clip.w;
    float front = frame.extra.x > 0.5 ? depth : depth * 0.5 + 0.5;
    front *= 0.05;
    clip.z = (frame.extra.x > 0.5 ? front : front * 2.0 - 1.0) * clip.w;
    gl_Position = clip;
}
