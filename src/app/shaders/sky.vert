#version 440

// One triangle covering the screen; each corner carries the view ray through it.
layout(location = 0) out vec4 vRay;

layout(std140, binding = 0) uniform Sky {
    // Inverse of projection x view rotation (the camera at the origin).
    mat4 inverseViewProjection;
    vec4 zenith;
    vec4 horizon;
    vec4 glow;
    // xyz: towards the sun / the moon; sunDirection.w: extra brightness of the sun (HDR, 0 in the
    // classic mode); moonDirection.w: moon phase (0 new, 0.5 full).
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 cloudColor;
    // x: star visibility, y: angle of the turning star field, z/w: cloud drift (metres).
    vec4 params;
    // xyz: camera position, w: height of the cloud layer.
    vec4 camera;
} sky;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vRay = sky.inverseViewProjection * vec4(corner, 0.5, 1.0);
    gl_Position = vec4(corner, 0.5, 1.0);
}
