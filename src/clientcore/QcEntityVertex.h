#pragma once

#include <QtGlobal>

#include <array>

// Vertex of creature, player, item and hand meshes: like QcBlockVertex plus a tint colour that the
// texture is multiplied with (outfit colours, the red flash of a hit).
struct QcEntityVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float shade = 1.0f;
    float skyLight = 1.0f;
    // RGBA, 255 = unchanged.
    std::array<quint8, 4> tint = {255, 255, 255, 255};
};
