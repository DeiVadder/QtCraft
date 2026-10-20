#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
// x: face shading and ambient occlusion, y: sky light, z: block light (unsigned normalised bytes);
// w: the surface byte (decision E-390: normal in the low three bits, material class in the high five).
layout(location = 2) in vec4 light;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vLight;
layout(location = 2) out float vDistance;
// 1 on alpha-tested blocks drawn in the opaque pass (leaves), else 0.
layout(location = 3) out float vCutout;
layout(location = 4) out vec3 vPosition;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
    // w: floor light per unit of sky light
    vec4 extra;
} frame;

// Block light (torches) is warm and does not follow the time of day.
const vec3 blockTint = vec3(1.0, 0.86, 0.66);
// QcSurfaceMaterial::Cutout in the high five bits of the surface byte.
const float cutoutMaterial = 1.0;
// Brightness of leaves varies by this much from corner to corner (decision E-724).
const float foliageVariation = 0.25;

// A value in [0, 1) for a block corner, the same for every face that shares it.
float cornerHash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1) * 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main()
{
    vTexCoord = texCoord;
    // The floor rises with the sky light (decision E-723): nights in the open and under trees are
    // not black, caves without sky light keep the minimum.
    float sky = max(light.y * frame.params.z, frame.params.w + light.y * frame.extra.w);
    vLight = light.x * max(vec3(sky), light.z * blockTint);
    float material = floor(floor(light.w * 255.0 + 0.5) / 8.0);
    vCutout = material == cutoutMaterial ? 1.0 : 0.0;
    // Leaves get a little lighter and darker from corner to corner, so a crown is not one flat
    // green (decision E-724).
    vLight *= 1.0 - vCutout * foliageVariation * cornerHash(position);
    vDistance = length(position - frame.cameraPosition.xyz);
    vPosition = position;
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
