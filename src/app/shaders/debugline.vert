// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

// One corner of a line drawn as a screen-aligned quad of lineWidth pixels: the quad's two ends are
// `position` and `other`; the sign of `side` picks the edge this corner lies on.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 other;
layout(location = 2) in float side;
layout(location = 3) in vec4 color;

layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
} frame;

const float lineWidth = 3.0;
const float nearW = 0.01;

// Moves `from` towards `to` until it lies in front of the camera, so a line that reaches behind
// the camera keeps its direction on screen. `to` lies in front (main() drops lines without such
// an end), so the two w differ.
vec4 inFront(vec4 from, vec4 to)
{
    if (from.w >= nearW) {
        return from;
    }
    return mix(from, to, (nearW - from.w) / (to.w - from.w));
}

void main()
{
    vColor = color;
    vec4 a = frame.viewProjection * vec4(position, 1.0);
    vec4 b = frame.viewProjection * vec4(other, 1.0);
    // Wholly behind the camera: all corners on one point outside the clip volume, nothing drawn.
    if (a.w < nearW && b.w < nearW) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec4 here = inFront(a, b);
    vec4 there = inFront(b, a);

    vec2 viewport = max(frame.extra.yz, vec2(1.0));
    vec2 screenHere = here.xy / here.w * viewport;
    vec2 screenThere = there.xy / there.w * viewport;
    vec2 along = screenThere - screenHere;
    vec2 direction = length(along) > 0.0001 ? normalize(along) : vec2(1.0, 0.0);
    vec2 normal = vec2(-direction.y, direction.x);

    here.xy += normal * side * lineWidth / viewport * here.w;
    gl_Position = here;
}
