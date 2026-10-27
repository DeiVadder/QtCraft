#version 440

// One halving of the bloom chain (decision E-899): the dual-filter downsample, the centre and the
// four diagonal neighbours one source texel away, each a bilinear tap over four texels. With
// QC_PREFILTER the first halving reads the HDR scene: display values made linear, clamped against
// single bright pixels flickering, and only what exceeds the threshold (with a soft knee) kept.

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Bloom {
    // xy: one texel of the source, zw: one pixel of the target (both in texture coordinates)
    vec4 texel;
    // x: threshold, y: soft knee, z: scatter (upsampling), w: brightest value taken from the scene
    vec4 params;
} bloom;

layout(binding = 1) uniform sampler2D source;

vec3 tap(vec2 uv)
{
    vec3 c = texture(source, uv).rgb;
#ifdef QC_PREFILTER
    // A broken pixel (NaN or infinity) would spread over the whole bloom.
    if (any(isnan(c)) || any(isinf(c)))
        c = vec3(0.0);
    c = min(pow(max(c, vec3(0.0)), vec3(2.2)), vec3(bloom.params.w));
#endif
    return c;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * bloom.texel.zw;
    vec2 t = bloom.texel.xy;
    vec3 sum = tap(uv) * 4.0;
    sum += tap(uv + vec2(-t.x, -t.y));
    sum += tap(uv + vec2(t.x, -t.y));
    sum += tap(uv + vec2(-t.x, t.y));
    sum += tap(uv + vec2(t.x, t.y));
    vec3 c = sum / 8.0;
#ifdef QC_PREFILTER
    float threshold = bloom.params.x;
    float knee = bloom.params.y;
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    c *= max(soft, brightness - threshold) / max(brightness, 1e-4);
#endif
    fragColor = vec4(c, 1.0);
}
