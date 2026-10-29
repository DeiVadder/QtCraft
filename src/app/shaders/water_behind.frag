// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

// The copy of the scene the water of the realistic mode looks through (decision E-906): the
// scene's colour (display values, beyond 1 where it shines) and in alpha the distance from the
// camera in blocks, from the depth; the sky (depth 1) gets farDistance.

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

// The Water block of water.frag (QcWaterUniforms); the copy reads the matrix, the depth convention
// and the pixel size.
layout(std140, binding = 0) uniform Water {
    mat4 inverseViewProjection;
    vec4 zenith;
    vec4 horizon;
    vec4 glow;
    vec4 toLight;
    vec4 lightColour;
    vec4 camera;
    vec4 pixel;
    vec4 effects;
    vec4 extinction;
    vec4 deep;
} water;

layout(binding = 1) uniform sampler2D scene;
layout(binding = 2) uniform sampler2D depth;

// Beyond every distance of the world, still exact in a half float.
const float farDistance = 60000.0;

void main()
{
    vec2 uv = gl_FragCoord.xy * water.pixel.xy;
    vec3 colour = texture(scene, uv).rgb;
    if (any(isnan(colour)) || any(isinf(colour)))
        colour = vec3(0.0);
    float d = texture(depth, uv).r;
    float distance = farDistance;
    if (d < 1.0) {
        float z = water.horizon.w > 0.5 ? d : d * 2.0 - 1.0;
        vec4 point = water.inverseViewProjection * vec4(vNdc, z, 1.0);
        distance = min(length(point.xyz / point.w), farDistance);
        if (isnan(distance))
            distance = farDistance;
    }
    fragColor = vec4(max(colour, vec3(0.0)), distance);
}
