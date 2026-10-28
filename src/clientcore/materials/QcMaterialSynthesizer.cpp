#include "clientcore/materials/QcMaterialSynthesizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace QcMaterialSynthesizer {

namespace {

// Texels below this alpha are holes (cut-out textures): no relief.
constexpr int holeAlpha = 128;
// Slope of the normals per unit of brightness between neighbours at 16 x 16, before `bump`.
constexpr float slope = 1.2f;
// How deep the darkest texels' ambient occlusion goes, and its floor.
constexpr float occlusionDepth = 0.35f;
constexpr float occlusionFloor = 0.75f;

float luminance(QColor colour)
{
    return (0.2126f * colour.redF()) + (0.7152f * colour.greenF())
           + (0.0722f * colour.blueF());
}

float distanceSquared(QColor a, QColor b)
{
    const float red = a.redF() - b.redF();
    const float green = a.greenF() - b.greenF();
    const float blue = a.blueF() - b.blueF();
    return (red * red) + (green * green) + (blue * blue);
}

int toByte(float value)
{
    return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
}

// What one texel of the picture is: its height, whether it is a hole or part of the inclusion.
struct Texel
{
    float height = 0.0f;
    float brightness = 0.0f;
    bool hole = false;
    bool inclusion = false;
};

// The texels of `colour`, holes at the mean height of the others.
struct Relief
{
    int size = 0;
    std::vector<Texel> texels;
    float mean = 0.0f;

    [[nodiscard]] const Texel &at(int x, int y) const
    {
        const int wrappedX = ((x % size) + size) % size;
        const int wrappedY = ((y % size) + size) % size;
        return texels[(static_cast<std::size_t>(wrappedY) * static_cast<std::size_t>(size)) + static_cast<std::size_t>(wrappedX)];
    }
};

Relief reliefOf(const QImage &colour, const QcMaterialParams &params, const Colours &colours)
{
    Relief relief;
    relief.size = colour.width();
    relief.texels.resize(static_cast<std::size_t>(relief.size) * static_cast<std::size_t>(relief.size));
    float sum = 0.0f;
    int counted = 0;
    for (int y = 0; y < relief.size; ++y) {
        for (int x = 0; x < relief.size; ++x) {
            const QColor pixel = colour.pixelColor(x, y);
            Texel &texel = relief.texels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(relief.size)) + static_cast<std::size_t>(x)];
            texel.hole = pixel.alpha() < holeAlpha;
            texel.brightness = luminance(pixel);
            texel.inclusion = params.inclusion.has_value() && !texel.hole
                              && distanceSquared(pixel, colours.secondary) < distanceSquared(pixel, colours.primary);
            texel.height = texel.brightness + (texel.inclusion ? params.inclusion->raise : 0.0f);
            if (!texel.hole) {
                sum += texel.height;
                ++counted;
            }
        }
    }
    relief.mean = counted > 0 ? sum / static_cast<float>(counted) : 0.0f;
    for (Texel &texel : relief.texels) {
        if (texel.hole) {
            texel.height = relief.mean;
        }
    }
    return relief;
}

// The brightness above which a texel glows: the brightest `share` of the solid texels.
float emissionThreshold(const Relief &relief, float share)
{
    std::vector<float> values;
    for (const Texel &texel : relief.texels) {
        if (!texel.hole) {
            values.push_back(texel.brightness);
        }
    }
    if (values.empty() || share >= 1.0f) {
        return -1.0f;
    }
    std::ranges::sort(values);
    const auto index = static_cast<std::size_t>(std::clamp((1.0f - share) * static_cast<float>(values.size()), 0.0f,
                                                           static_cast<float>(values.size() - 1)));
    // Texels at the threshold itself stay dark: a plain picture does not glow half.
    return values[index];
}

QRgb normalTexel(const Relief &relief, int x, int y, float strength)
{
    const Texel &texel = relief.at(x, y);
    if (texel.hole) {
        return flatNormal;
    }
    // Central differences, y up in the picture (towards row 0).
    const float right = relief.at(x + 1, y).height - relief.at(x - 1, y).height;
    const float up = relief.at(x, y - 1).height - relief.at(x, y + 1).height;
    float nx = -right * strength;
    float ny = -up * strength;
    const float length = std::sqrt((nx * nx) + (ny * ny) + 1.0f);
    nx /= length;
    ny /= length;
    const float below = std::max(relief.mean - texel.height, 0.0f);
    const float occlusion = std::max(1.0f - (occlusionDepth * std::min(strength / slope, 1.0f) * below), occlusionFloor);
    const float height = std::clamp(texel.height, 0.0f, 1.0f);
    return qRgba(toByte((nx * 0.5f) + 0.5f), toByte((ny * 0.5f) + 0.5f), toByte(occlusion), toByte(height));
}

QcMaterialParams inclusionParams(const QcMaterialParams &params, const QcMaterialParams::Inclusion &inclusion)
{
    QcMaterialParams ore = params;
    ore.smoothness = inclusion.smoothness;
    ore.reflectance = inclusion.reflectance;
    ore.metal = inclusion.metal;
    ore.emission = inclusion.emission;
    return ore;
}

} // namespace

QColor specularTexel(const QcMaterialParams &params, float emission)
{
    const int f0 = params.metal != QcMaterialParams::Metal::None ? static_cast<int>(params.metal) : std::clamp(params.reflectance, 0, 229);
    const int porosity = std::clamp(static_cast<int>(std::lround(params.porosity * 64.0f)), 0, 64);
    // In the atlas 0 is "no light" (labPBR's 255 is turned into 0 when a pack's map is read), so
    // filtering and mip levels can average the light.
    const int light = emission > 0.0f ? std::clamp(static_cast<int>(std::lround(emission * 255.0f)), 1, 255) : 0;
    return {toByte(params.smoothness), f0, porosity, light};
}

Maps paint(const QImage &colour, const QcMaterialParams &params, const Colours &colours)
{
    const QImage picture = colour.convertToFormat(QImage::Format_RGBA8888);
    const int size = picture.width();
    Maps maps{QImage(size, size, QImage::Format_RGBA8888), QImage(size, size, QImage::Format_RGBA8888)};
    if (size <= 0) {
        return maps;
    }
    const Relief relief = reliefOf(picture, params, colours);
    // Neighbours at higher resolutions lie closer: a gentler step per texel, kept from vanishing.
    const float strength = params.bump * slope * std::sqrt(static_cast<float>(size) / 16.0f);
    const float threshold = params.emission > 0.0f ? emissionThreshold(relief, params.emissionTexels) : 0.0f;
    const QcMaterialParams ore = params.inclusion ? inclusionParams(params, *params.inclusion) : params;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            maps.normal.setPixel(x, y, normalTexel(relief, x, y, strength));
            const Texel &texel = relief.at(x, y);
            const QcMaterialParams &own = texel.inclusion ? ore : params;
            float emission = 0.0f;
            if (own.emission > 0.0f && !texel.hole) {
                // The glowing texels fade in over a little brightness above the threshold, not as a hard edge.
                const float above = std::clamp((texel.brightness - threshold) / 0.08f, 0.0f, 1.0f);
                emission = texel.inclusion ? own.emission : own.emission * above;
            }
            maps.specular.setPixelColor(x, y, specularTexel(own, emission));
        }
    }
    return maps;
}

QImage packEmission(const QImage &specular)
{
    QImage converted = specular.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < converted.height(); ++y) {
        for (int x = 0; x < converted.width(); ++x) {
            const QRgb texel = converted.pixel(x, y);
            if (qAlpha(texel) == 255) {
                converted.setPixel(x, y, qRgba(qRed(texel), qGreen(texel), qBlue(texel), 0));
            }
        }
    }
    return converted;
}

Maps flat(int size)
{
    Maps maps{QImage(size, size, QImage::Format_RGBA8888), QImage(size, size, QImage::Format_RGBA8888)};
    maps.normal.fill(QColor::fromRgba(flatNormal));
    maps.specular.fill(specularTexel(QcMaterialParams::flat(), 0.0f));
    return maps;
}

} // namespace QcMaterialSynthesizer
