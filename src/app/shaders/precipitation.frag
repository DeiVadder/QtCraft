#version 440

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vShape;
layout(location = 2) in float vDistance;
layout(location = 3) in float vSnow;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
} frame;

void main()
{
    // A snowflake is round with a soft edge; a streak is drawn whole.
    float shapeAlpha = vSnow > 0.5 ? 1.0 - smoothstep(0.45, 1.0, length(vShape)) : 1.0;
    // In the fog the particles take its colour and fade out, as the world behind them does.
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
    fragColor = vec4(mix(vColor.rgb, frame.fogColor.rgb, fog), vColor.a * shapeAlpha * (1.0 - fog));
}
