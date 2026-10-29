// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preview/QcWorldMapPainter.h"

#include "common/content/registry/QcContentRegistry.h"

#include <QColor>

#include <algorithm>
#include <cmath>

namespace {

constexpr quint32 shallowWater = 0x4f8fd6;
constexpr quint32 deepWater = 0x1b3a73;
// Still water whose top layer is ice; a frozen river keeps its river biome's colour.
constexpr quint32 ice = 0xb9dff0;
// Water this deep or deeper has the deep colour.
constexpr double fullDepth = 24.0;

QColor mixed(QColor a, QColor b, double share)
{
    const auto mix = [share](double x, double y) { return x + (y - x) * share; };
    return QColor::fromRgbF(static_cast<float>(mix(a.redF(), b.redF())), static_cast<float>(mix(a.greenF(), b.greenF())),
                            static_cast<float>(mix(a.blueF(), b.blueF())));
}

QColor scaled(QColor colour, double factor)
{
    const auto channel = [factor](float value) { return static_cast<float>(std::clamp(value * factor, 0.0, 1.0)); };
    return QColor::fromRgbF(channel(colour.redF()), channel(colour.greenF()), channel(colour.blueF()));
}

// Light from the north-west: brighter where the land rises towards the south-east neighbour's
// opposite, darker on slopes facing away; the difference per block, so zooming keeps the relief.
double hillShade(const QcBiomeMap &map, int x, int z)
{
    const QcBiomeMap::Cell &cell = map.cell(x, z);
    const QcBiomeMap::Cell &northWest = map.cell(std::max(0, x - 1), std::max(0, z - 1));
    const double slope = static_cast<double>(cell.height - northWest.height) / map.area().blocksPerCell;
    return 1.0 + std::clamp(slope * 0.3, -0.35, 0.35);
}

QColor cellColour(const QcBiomeMap &map, int x, int z, const QList<quint32> &colours, int seaLevel)
{
    const QcBiomeMap::Cell &cell = map.cell(x, z);
    const QColor biome = cell.biome >= 0 && cell.biome < colours.size() ? QColor::fromRgb(colours.at(cell.biome)) : QColor(Qt::magenta);
    if (cell.frozen && cell.water == QcBiomeMap::Water::Sea) {
        return QColor::fromRgb(ice);
    }
    switch (cell.water) {
    case QcBiomeMap::Water::Sea:
        return mixed(QColor::fromRgb(shallowWater), QColor::fromRgb(deepWater), std::min(1.0, (seaLevel - cell.height) / fullDepth));
    case QcBiomeMap::Water::River:
        return scaled(biome, 1.15);
    case QcBiomeMap::Water::Land:
        break;
    }
    const double height = std::clamp(0.9 + (cell.height - seaLevel) * 0.003, 0.85, 1.2);
    return scaled(biome, height * hillShade(map, x, z));
}

} // namespace

namespace QcWorldMapPainter {

QList<quint32> biomeColours(const QcContentRegistry &content)
{
    QList<quint32> colours;
    const auto &biomes = content.worldgen().biomes();
    for (qsizetype i = 0; i < biomes.size(); ++i) {
        // Evenly spread hues for a biome without its own colour (a mod's).
        const QColor spread = QColor::fromHsvF(std::fmod(static_cast<float>(i) * 0.381966f, 1.0f), 0.55f, 0.75f);
        colours.append(biomes.at(i).mapColour.value_or(spread.rgb() & 0xffffff));
    }
    return colours;
}

quint32 seaColour()
{
    return shallowWater;
}

quint32 iceColour()
{
    return ice;
}

QImage paint(const QcBiomeMap &map, const QList<quint32> &colours, int seaLevel)
{
    QImage image(map.size(), map.size(), QImage::Format_RGB32);
    for (int z = 0; z < map.size(); ++z) {
        for (int x = 0; x < map.size(); ++x) {
            image.setPixelColor(x, z, cellColour(map, x, z, colours, seaLevel));
        }
    }
    return image;
}

} // namespace QcWorldMapPainter
