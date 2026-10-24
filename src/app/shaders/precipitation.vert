#version 440

// One corner of a raindrop's streak or a snowflake's dot (decision E-397), drawn instanced: the
// six corners of a quad are the same for every particle, the instance gives the particle.
// A raindrop is a screen-aligned quad from its position up along `shape.streak`, `shape.streak.w`
// pixels to each side; a snowflake is a square facing the camera around a point half a flake above
// its position, `shape.flake.x` metres wide but between `shape.flake.y` and `shape.flake.z` pixels
// in radius on screen, so near flakes stay small dots and far ones do not vanish. Pixel sizes are
// those of a picture 1080 pixels high and grow with the output's height.
layout(location = 0) in vec2 corner;
// The drop relative to the origin of the frame's drops (shape.origin).
layout(location = 1) in vec3 dropPosition;
// x: above 0 for a snowflake (QcPrecipitationDrop::snow), 0 for a raindrop; y: opacity
layout(location = 2) in vec4 kind;

layout(location = 0) out vec4 vColor;
// Where in the dot this corner lies (-1..1 on both axes), for the round shape; 0 for rain.
layout(location = 1) out vec2 vShape;
layout(location = 2) out float vDistance;
layout(location = 3) out float vSnow;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
    // y, z: the output size in pixels
    vec4 extra;
} frame;

layout(std140, binding = 1) uniform Shape {
    // xyz: a raindrop's streak in metres, w: half its width in pixels
    vec4 streak;
    // x: a flake's diameter in metres, y and z: smallest and largest radius in pixels
    vec4 flake;
    vec4 rainColor;
    vec4 snowColor;
    // xyz: the offset of the drops' origin from the camera (decision E-883); the drops are relative
    // to it, so `dropPosition + shape.origin.xyz` is relative to the camera.
    vec4 origin;
} shape;

const float nearW = 0.01;

// The drop relative to the camera (decision E-883), set first in main().
vec3 position;

// Moves `from` towards `to` until it lies in front of the camera, so a streak that reaches behind
// the camera keeps its direction on screen.
vec4 inFront(vec4 from, vec4 to)
{
    if (from.w >= nearW) {
        return from;
    }
    return mix(from, to, (nearW - from.w) / (to.w - from.w));
}

// Pixel sizes are given for a picture 1080 pixels high.
float pixelScale()
{
    return max(frame.extra.z, 1.0) / 1080.0;
}

vec4 rainCorner(vec2 halfViewport)
{
    vec4 a = frame.viewProjection * vec4(position, 1.0);
    vec4 b = frame.viewProjection * vec4(position + shape.streak.xyz, 1.0);
    if (a.w < nearW && b.w < nearW) {
        return vec4(2.0, 2.0, 2.0, 1.0);
    }

    vec4 here = corner.y < 0.0 ? inFront(a, b) : inFront(b, a);
    vec4 there = corner.y < 0.0 ? inFront(b, a) : inFront(a, b);
    vec2 along = there.xy / there.w * halfViewport - here.xy / here.w * halfViewport;
    vec2 direction = length(along) > 0.0001 ? normalize(along) : vec2(0.0, 1.0);
    vec2 normal = vec2(-direction.y, direction.x);
    here.xy += normal * corner.x * shape.streak.w * pixelScale() / halfViewport * here.w;
    return here;
}

vec4 snowCorner(vec2 halfViewport)
{
    float radius = shape.flake.x * 0.5;
    vec3 middle = position + vec3(0.0, radius, 0.0);
    vec4 centre = frame.viewProjection * vec4(middle, 1.0);
    if (centre.w < nearW) {
        return vec4(2.0, 2.0, 2.0, 1.0);
    }

    // The flake's radius on screen: its world size seen at its distance, kept within the limits.
    vec3 toFlake = middle - frame.cameraPosition.xyz;
    vec3 side = normalize(cross(toFlake, vec3(0.0, 1.0, 0.0)) + vec3(1e-6, 0.0, 0.0));
    vec4 edge = frame.viewProjection * vec4(middle + side * radius, 1.0);
    float pixels = length((edge.xy / max(edge.w, nearW) - centre.xy / centre.w) * halfViewport);
    pixels = clamp(pixels, shape.flake.y * pixelScale(), shape.flake.z * pixelScale());
    centre.xy += corner * pixels / halfViewport * centre.w;
    return centre;
}

void main()
{
    position = dropPosition + shape.origin.xyz;
    vec2 halfViewport = max(frame.extra.yz, vec2(1.0)) * 0.5;
    vSnow = kind.x > 0.0 ? 1.0 : 0.0;
    vShape = kind.x > 0.0 ? corner : vec2(0.0);
    vDistance = length(position - frame.cameraPosition.xyz);
    // The particles take the sky's light (night, overcast, a flash), never darker than the minimum.
    float light = max(frame.params.z, frame.params.w);
    vec3 colour = kind.x > 0.0 ? shape.snowColor.rgb : shape.rainColor.rgb;
    vColor = vec4(colour * light, kind.y);
    gl_Position = kind.x > 0.0 ? snowCorner(halfViewport) : rainCorner(halfViewport);
}
