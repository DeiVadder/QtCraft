// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/materials/QcMaterialMips.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace QcMaterialMips {

namespace {

constexpr int metalCodes = 230;

float decode(int byte)
{
    return (static_cast<float>(byte) / 127.5f) - 1.0f;
}

int toByte(float value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

struct Normal
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Normal unpack(QRgb texel)
{
    const float x = decode(qRed(texel));
    const float y = decode(qGreen(texel));
    return {x, y, std::sqrt(std::max(1.0f - (x * x) - (y * y), 0.0f))};
}

// The averaged normal of four texels and its length (1: all alike).
struct Average
{
    QRgb texel = 0;
    float length = 1.0f;
};

Average averageNormals(std::array<QRgb, 4> texels)
{
    Normal sum;
    int occlusion = 0;
    int height = 0;
    for (const QRgb texel : texels) {
        const Normal n = unpack(texel);
        sum.x += n.x;
        sum.y += n.y;
        sum.z += n.z;
        occlusion += qBlue(texel);
        height += qAlpha(texel);
    }
    const float length = std::sqrt((sum.x * sum.x) + (sum.y * sum.y) + (sum.z * sum.z)) / 4.0f;
    const float scale = length > 1e-6f ? 1.0f / (4.0f * length) : 0.0f;
    const float x = length > 1e-6f ? sum.x * scale : 0.0f;
    const float y = length > 1e-6f ? sum.y * scale : 0.0f;
    return {qRgba(toByte(((x * 0.5f) + 0.5f) * 255.0f), toByte(((y * 0.5f) + 0.5f) * 255.0f), toByte(static_cast<float>(occlusion) / 4.0f),
                  toByte(static_cast<float>(height) / 4.0f)),
            std::clamp(length, 1e-3f, 1.0f)};
}

QRgb averageSpecular(std::array<QRgb, 4> texels, float normalLength)
{
    float roughnessSquared = 0.0f;
    int reflectance = 0;
    int dielectrics = 0;
    int porosity = 0;
    float emission = 0.0f;
    std::array<int, 4> metals{};
    int metalCount = 0;
    for (const QRgb texel : texels) {
        const float roughness = (1.0f - (static_cast<float>(qRed(texel)) / 255.0f)) * (1.0f - (static_cast<float>(qRed(texel)) / 255.0f));
        roughnessSquared += roughness * roughness;
        if (qGreen(texel) >= metalCodes) {
            metals.at(static_cast<std::size_t>(metalCount)) = qGreen(texel);
            ++metalCount;
        } else {
            reflectance += qGreen(texel);
            ++dielectrics;
        }
        porosity += qBlue(texel);
        emission += static_cast<float>(qAlpha(texel));
    }
    // Toksvig: the spread of the normals adds to the roughness.
    const float spread = (1.0f - normalLength) / normalLength;
    const float roughness = std::sqrt(std::sqrt(std::min((roughnessSquared / 4.0f) + spread, 1.0f)));
    const int smoothness = toByte((1.0f - roughness) * 255.0f);
    const int f0 = metalCount >= 2 ? *std::ranges::max_element(metals) : toByte(static_cast<float>(reflectance) / static_cast<float>(std::max(dielectrics, 1)));
    const float light = emission / 4.0f;
    return qRgba(smoothness, f0, toByte(static_cast<float>(porosity) / 4.0f), toByte(light));
}

std::array<QRgb, 4> block(const QImage &image, int x, int y)
{
    const int right = std::min(x + 1, image.width() - 1);
    const int below = std::min(y + 1, image.height() - 1);
    return {image.pixel(x, y), image.pixel(right, y), image.pixel(x, below), image.pixel(right, below)};
}

} // namespace

Level halve(const QImage &normal, const QImage &specular)
{
    const QImage normals = normal.convertToFormat(QImage::Format_RGBA8888);
    const QImage speculars = specular.convertToFormat(QImage::Format_RGBA8888);
    const int width = std::max(1, normals.width() / 2);
    const int height = std::max(1, normals.height() / 2);
    Level level{QImage(width, height, QImage::Format_RGBA8888), QImage(width, height, QImage::Format_RGBA8888)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Average average = averageNormals(block(normals, 2 * x, 2 * y));
            level.normal.setPixel(x, y, average.texel);
            level.specular.setPixel(x, y, averageSpecular(block(speculars, 2 * x, 2 * y), average.length));
        }
    }
    return level;
}

namespace {

QImage enlarged(const QImage &image, int size)
{
    return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888);
}

QImage plain(QSize size, QRgb texel)
{
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(QColor::fromRgba(texel));
    return image;
}

// A flat normal and a rough dielectric without light: partners that do not change the other map.
constexpr QRgb flatNormal = 0xFF8080FFu;
constexpr QRgb plainSpecular = 0x00000A00u;

} // namespace

QImage normalAt(const QImage &normal, int size)
{
    QImage level = normal.convertToFormat(QImage::Format_RGBA8888);
    while (level.width() > size && level.width() >= 2) {
        level = halve(level, plain(level.size(), plainSpecular)).normal;
    }
    return level.width() == size ? level : enlarged(level, size);
}

QImage specularAt(const QImage &specular, int size)
{
    QImage level = specular.convertToFormat(QImage::Format_RGBA8888);
    while (level.width() > size && level.width() >= 2) {
        level = halve(plain(level.size(), flatNormal), level).specular;
    }
    return level.width() == size ? level : enlarged(level, size);
}

} // namespace QcMaterialMips
