#pragma once

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

// Vertex layout shared by the mesher and the GPU pipeline: position and atlas coordinates as
// floats, shading and light as unsigned normalised bytes (value × 255, read as one UNormByte4
// attribute), 24 bytes per vertex. Light levels k / 15 are stored exactly (17 k); other values to
// within 1 / 510.
struct QcBlockVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    // Face shading and ambient occlusion, sky light (scaled by the time of day in the shader),
    // block light (torches, independent of the time of day) and an unused byte.
    std::array<quint8, 4> light = {255, 255, 0, 0};

    static constexpr std::size_t shadeIndex = 0;
    static constexpr std::size_t skyLightIndex = 1;
    static constexpr std::size_t blockLightIndex = 2;

    static quint8 pack(float value) { return quint8(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }
    static float unpack(quint8 value) { return float(value) / 255.0f; }

    // Shading in [0, 1].
    float shade() const { return unpack(light[shadeIndex]); }
    void setShade(float value) { light[shadeIndex] = pack(value); }
    // Sky light in [0, 1].
    float skyLight() const { return unpack(light[skyLightIndex]); }
    void setSkyLight(float value) { light[skyLightIndex] = pack(value); }
    // Block light in [0, 1].
    float blockLight() const { return unpack(light[blockLightIndex]); }
    void setBlockLight(float value) { light[blockLightIndex] = pack(value); }
};
static_assert(sizeof(QcBlockVertex) == 24);
