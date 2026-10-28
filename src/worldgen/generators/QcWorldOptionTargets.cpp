#include "worldgen/generators/QcWorldOptionTargets.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using QcWorldOptionTargets::Kind;
using QcWorldOptionTargets::Target;

// Most vein starts an ore may get per chunk (as QcWorldgenParser allows in ores/*.json).
constexpr int maxVeinsPerChunk = 256;

// Changes the terrain settings of `content` with `change(QcTerrainSettings &)`.
template<typename Change>
void changeTerrain(QcContentRegistry &content, Change change)
{
    QcTerrainSettings terrain = content.worldgen().terrainSettings();
    change(terrain);
    content.worldgen().setTerrainSettings(std::move(terrain));
}

// Changes the village settings of `content` with `change(QcVillageSettings &)`.
template<typename Change>
void changeVillages(QcContentRegistry &content, Change change)
{
    QcVillageSettings villages = content.villageCatalog().settings();
    change(villages);
    content.villageCatalog().setSettings(std::move(villages));
}

// An ore with its vein starts scaled by `factor`; at 1 they stay exactly as they were.
QcOreDefinition scaledOre(QcOreDefinition ore, double factor)
{
    ore.veinsPerChunk = std::clamp(static_cast<int>(std::lround(ore.veinsPerChunk * factor)), 0, maxVeinsPerChunk);
    return ore;
}

std::optional<double> readVillageSpacing(const QcContentRegistry &content, const QString &)
{
    return content.villageCatalog().settings().spacing;
}

// A village keeps its margin of 16 blocks on both sides of its radius (QcVillageParser).
void writeVillageSpacing(QcContentRegistry &content, const QString &, double value)
{
    changeVillages(content, [value](QcVillageSettings &villages) {
        villages.spacing = std::max(static_cast<int>(std::lround(value)), 2 * (villages.radius + 16));
    });
}

std::optional<double> readVillageChance(const QcContentRegistry &content, const QString &)
{
    return content.villageCatalog().settings().chance;
}

void writeVillageChance(QcContentRegistry &content, const QString &, double value)
{
    changeVillages(content, [value](QcVillageSettings &villages) { villages.chance = value; });
}

std::optional<double> readBiomeScale(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().biomeScale;
}

void writeBiomeScale(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) { terrain.biomeScale = value; });
}

std::optional<double> readCaves(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().caves ? 1.0 : 0.0;
}

void writeCaves(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) { terrain.caves = value != 0.0; });
}

std::optional<double> readRivers(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().rivers ? 1.0 : 0.0;
}

// Deltas grow only from rivers: without rivers there are none either (and no warning about it).
void writeRivers(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) {
        terrain.rivers = value != 0.0;
        terrain.deltas = terrain.deltas && terrain.rivers;
    });
}

std::optional<double> readFactor(const QcContentRegistry &, const QString &)
{
    return 1.0;
}

void writeOreAmount(QcContentRegistry &content, const QString &, double value)
{
    const QList<QcOreDefinition> ores = content.worldgen().ores();
    for (const QcOreDefinition &ore : ores) {
        content.worldgen().registerOre(scaledOre(ore, value));
    }
}

std::optional<double> readOneOre(const QcContentRegistry &content, const QString &subject)
{
    const QList<QcOreDefinition> &ores = content.worldgen().ores();
    return std::ranges::contains(ores, subject, &QcOreDefinition::name) ? std::optional<double>(1.0) : std::nullopt;
}

void writeOneOre(QcContentRegistry &content, const QString &subject, double value)
{
    const QList<QcOreDefinition> &ores = content.worldgen().ores();
    if (const auto ore = std::ranges::find(ores, subject, &QcOreDefinition::name); ore != ores.end()) {
        content.worldgen().registerOre(scaledOre(*ore, value));
    }
}

std::optional<double> readDungeonChance(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().dungeons.chance;
}

void writeDungeonChance(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) { terrain.dungeons.chance = value; });
}

std::optional<double> readMineshaftChance(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().mineshafts.chance;
}

void writeMineshaftChance(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) { terrain.mineshafts.chance = value; });
}

std::optional<double> readTowerChance(const QcContentRegistry &content, const QString &)
{
    return content.worldgen().terrainSettings().coastalTowers.chance;
}

void writeTowerChance(QcContentRegistry &content, const QString &, double value)
{
    changeTerrain(content, [value](QcTerrainSettings &terrain) { terrain.coastalTowers.chance = value; });
}

constexpr std::array targets{
    Target{u"villages.spacing", Kind::Number, false, 128.0, 4096.0, readVillageSpacing, writeVillageSpacing},
    Target{u"villages.chance", Kind::Number, false, 0.0, 1.0, readVillageChance, writeVillageChance},
    Target{u"terrain.biomeScale", Kind::Number, false, QcTerrainSettings::minBiomeScale, QcTerrainSettings::maxBiomeScale, readBiomeScale,
           writeBiomeScale},
    Target{u"terrain.caves", Kind::Switch, false, 0.0, 1.0, readCaves, writeCaves},
    Target{u"terrain.rivers", Kind::Switch, false, 0.0, 1.0, readRivers, writeRivers},
    Target{u"ores.amount", Kind::Number, false, 0.0, 8.0, readFactor, writeOreAmount},
    Target{u"ore.amount", Kind::Number, true, 0.0, 8.0, readOneOre, writeOneOre},
    Target{u"dungeons.chance", Kind::Number, false, 0.0, 1.0, readDungeonChance, writeDungeonChance},
    Target{u"mineshafts.chance", Kind::Number, false, 0.0, 1.0, readMineshaftChance, writeMineshaftChance},
    Target{u"coastalTowers.chance", Kind::Number, false, 0.0, 1.0, readTowerChance, writeTowerChance},
};

} // namespace

namespace QcWorldOptionTargets {

std::span<const Target> all()
{
    return targets;
}

const Target *find(QStringView name)
{
    const auto it = std::ranges::find(targets, name, &Target::name);
    return it == targets.end() ? nullptr : &*it;
}

} // namespace QcWorldOptionTargets
