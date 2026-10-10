#version 440

layout(location = 0) in vec4 vRay;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Sky {
    mat4 inverseViewProjection;
    vec4 zenith;
    vec4 horizon;
    vec4 glow;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 cloudColor;
    vec4 params;
    vec4 camera;
} sky;

const float sunRadius = 0.9994;
const float moonRadius = 0.045;
const float cloudCell = 40.0;

float hash3(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise2(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash3(vec3(cell, 0.0));
    float b = hash3(vec3(cell + vec2(1.0, 0.0), 0.0));
    float c = hash3(vec3(cell + vec2(0.0, 1.0), 0.0));
    float d = hash3(vec3(cell + vec2(1.0, 1.0), 0.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 dome(vec3 dir)
{
    float up = smoothstep(-0.02, 0.55, dir.y);
    vec3 color = mix(sky.horizon.rgb, sky.zenith.rgb, up);
    // Below the horizon the dome darkens a little (seen from mountains or in the air).
    color *= 1.0 - 0.35 * clamp(-dir.y * 3.0, 0.0, 1.0);
    // Sunrise and sunset light up the sky around the sun near the horizon.
    float towardsSun = max(dot(normalize(vec3(dir.x, 0.0, dir.z)), normalize(vec3(sky.sunDirection.x, 0.0, sky.sunDirection.z))), 0.0);
    color += sky.glow.rgb * pow(towardsSun, 5.0) * (1.0 - smoothstep(0.0, 0.45, abs(dir.y)));
    return color;
}

vec3 addStars(vec3 color, vec3 dir)
{
    if (sky.params.x <= 0.0 || dir.y < -0.05)
        return color;
    // The star field turns with the sun around the north-south axis.
    float c = cos(sky.params.y);
    float s = sin(sky.params.y);
    vec3 turned = vec3(c * dir.x + s * dir.y, -s * dir.x + c * dir.y, dir.z);
    vec3 cell = floor(turned * 180.0);
    float star = hash3(cell);
    if (star < 0.9965)
        return color;
    float brightness = (star - 0.9965) / 0.0035;
    return color + vec3(0.85, 0.88, 1.0) * brightness * sky.params.x * smoothstep(-0.05, 0.15, dir.y);
}

vec3 addSun(vec3 color, vec3 dir)
{
    float facing = dot(dir, sky.sunDirection.xyz);
    color += vec3(1.0, 0.85, 0.6) * pow(max(facing, 0.0), 300.0) * 0.5;
    float disc = smoothstep(sunRadius - 0.00025, sunRadius, facing);
    return mix(color, vec3(1.0, 0.97, 0.85), disc);
}

vec3 addMoon(vec3 color, vec3 dir)
{
    vec3 m = sky.moonDirection.xyz;
    if (dot(dir, m) <= 0.0)
        return color;
    vec3 right = normalize(cross(vec3(0.0, 0.0, 1.0), m));
    vec3 up = cross(m, right);
    vec2 local = vec2(dot(dir, right), dot(dir, up)) / moonRadius;
    float r2 = dot(local, local);
    if (r2 >= 1.0)
        return color;

    // A lit sphere: the light comes from behind at new moon and from the viewer at full moon.
    vec3 normal = vec3(local, sqrt(1.0 - r2));
    float angle = 6.2831853 * sky.moonDirection.w;
    vec3 light = vec3(sin(angle), 0.0, -cos(angle));
    float lit = smoothstep(-0.06, 0.06, dot(normal, light));
    float craters = 0.82 + 0.18 * noise2(local * 3.0 + 7.0);
    vec3 moon = mix(vec3(0.07, 0.08, 0.11), vec3(0.96, 0.95, 0.88) * craters, lit);
    float edge = 1.0 - smoothstep(0.9, 1.0, r2);
    return mix(color, moon, edge * mix(0.35, 1.0, lit));
}

vec3 addClouds(vec3 color, vec3 dir)
{
    float height = sky.camera.w - sky.camera.y;
    if (dir.y <= 0.01 || height <= 0.0)
        return color;
    float distance = height / dir.y;
    vec2 point = (sky.camera.xz + dir.xz * distance + sky.params.zw) / cloudCell;
    float density = noise2(point) * 0.65 + noise2(point * 2.7 + 3.1) * 0.35;
    float cover = smoothstep(0.58, 0.70, density);
    float fade = 1.0 - smoothstep(300.0, 1200.0, distance);
    return mix(color, sky.cloudColor.rgb, cover * fade * 0.85);
}

void main()
{
    vec3 dir = normalize(vRay.xyz / vRay.w);
    vec3 color = dome(dir);
    color = addStars(color, dir);
    color = addMoon(color, dir);
    color = addSun(color, dir);
    color = addClouds(color, dir);
    fragColor = vec4(color, 1.0);
}
