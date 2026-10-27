#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in float shade;
layout(location = 3) in float skyLight;
layout(location = 4) in vec4 tint;
layout(location = 5) in float blockLight;
// Per draw (QcDrawOrigins, decision E-883): xyz the mesh's origin relative to the render anchor;
// the vertices are relative to the origin (QcEntityMesh::origin).
layout(location = 6) in vec4 origin;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vLight;
layout(location = 2) out float vDistance;
layout(location = 3) out vec4 vTint;
#ifdef QC_SHADOWS
// For the sun shadows of the realistic mode (the _shadow variants, decision E-903): where the
// shadow map is read (relative to the camera), the parts of vLight (x shade, y the sky light of the
// time of day, z its floor, w the block light) and the moving lights.
layout(location = 4) out vec3 vShadowAt;
layout(location = 5) out vec4 vLightParts;
layout(location = 6) out vec3 vMoving;
#endif

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
    // x: 1 when clip-space depth runs from 0 to 1, w: floor light per unit of sky light
    vec4 extra;
    vec4 atlas;
    vec4 animation;
    // xyz: the camera relative to the render anchor (QcDrawOrigins::follow)
    vec4 drawCamera;
    // The moving lights of the realistic mode (decision E-902), the first animation.z of them: xyz
    // relative to the camera, w the level (0..15) plus 16 times the colour (0 warm, 1 glow).
    vec4 lights[16];
} frame;

// The moving lights (decision E-902), as QcMovingLights::brightness: one level less per block of
// distance from `p` (relative to the camera), a level of 15 is full brightness; the brightest one
// by colour wins, like block light. Nothing in the classic mode (no lights).
const vec3 warmLight = vec3(1.0, 0.86, 0.66);
const vec3 glowLight = vec3(0.78, 1.0, 0.42);

vec3 movingLight(vec3 p)
{
    vec3 sum = vec3(0.0);
    int count = int(frame.animation.z + 0.5);
    for (int i = 0; i < 16; ++i) {
        if (i >= count)
            break;
        vec4 light = frame.lights[i];
        float colour = floor(light.w / 16.0);
        float level = light.w - 16.0 * colour;
        float brightness = clamp((level - length(light.xyz - p)) / 15.0, 0.0, 1.0);
        sum = max(sum, brightness * (colour > 0.5 ? glowLight : warmLight));
    }
    return sum;
}

void main()
{
    vTexCoord = texCoord;
    // The floor rises with the sky light (decision E-723).
    float sky = max(skyLight * frame.params.z, frame.params.w + skyLight * frame.extra.w);
    // Block light (torches) is warm and does not follow the time of day.
    vLight = shade * max(vec3(sky), blockLight * vec3(1.0, 0.86, 0.66));
    vTint = tint;
    vec3 relative = position + origin.xyz - frame.drawCamera.xyz;
    // Torches in hands and fireflies nearby (realistic mode only).
    if (frame.animation.z > 0.5)
        vLight = max(vLight, shade * movingLight(relative));
#ifdef QC_SHADOWS
    vLightParts = vec4(shade, skyLight * frame.params.z, frame.params.w + skyLight * frame.extra.w, blockLight);
    vMoving = frame.animation.z > 0.5 ? shade * movingLight(relative) : vec3(0.0);
    vShadowAt = relative;
#endif
    vDistance = length(relative - frame.cameraPosition.xyz);
    gl_Position = frame.viewProjection * vec4(relative, 1.0);
}
