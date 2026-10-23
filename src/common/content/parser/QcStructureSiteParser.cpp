#include "common/content/parser/QcStructureSiteParser.h"

#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Structure = QcStructureDefinition;

// Regions of surface structures are whole chunks between these sizes.
constexpr int minSpacing = 64;
constexpr int maxSpacing = 4096;
constexpr int maxGuardians = 8;

QStringList readBiomes(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList biomes;
    const QJsonArray entries = json.value(u"biomes"_s).toArray();
    for (const auto &entry : entries) {
        const QString biome = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (biome.isEmpty()) {
            reader.fail(u"'biomes' must be an array of biome names"_s);
            return {};
        }
        biomes.append(biome);
    }
    if (biomes.isEmpty()) {
        reader.fail(u"needs 'biomes', an array of at least one biome name"_s);
    }
    return biomes;
}

[[nodiscard]] Structure::Surface parseSurface(const QJsonObject &json, const Structure &structure, QcJsonFieldReader &reader,
                                              const QString &packNamespace)
{
    Structure::Surface surface;
    surface.biomes = readBiomes(json, packNamespace, reader);
    surface.spacing = reader.integer(u"spacing"_s, surface.spacing, minSpacing, maxSpacing);
    surface.chance = reader.number(u"chance"_s, surface.chance, 0.0, 1.0);
    surface.attempts = reader.integer(u"attempts"_s, surface.attempts, 1, 16);
    surface.maxSlope = reader.integer(u"maxSlope"_s, surface.maxSlope, 0, 16);
    surface.villageMargin = reader.integer(u"villageMargin"_s, surface.villageMargin, 0, 128);
    surface.foundation = reader.name(u"foundation"_s, packNamespace);
    surface.waterDepth = reader.integer(u"waterDepth"_s, surface.waterDepth, 0, 16);
    surface.preferWater = reader.boolean(u"preferWater"_s, surface.preferWater);
    if (surface.preferWater && surface.waterDepth == 0) {
        reader.fail(u"'preferWater' needs a 'waterDepth'"_s);
    }
    surface.clearFrom = reader.integer(u"clearFrom"_s, surface.clearFrom, 0, std::max(0, structure.sizeY - 1));
    surface.clearTerrain = reader.boolean(u"clearTerrain"_s, surface.clearTerrain);
    if (surface.spacing % 16 != 0) {
        reader.fail(u"'spacing' must be whole chunks (a multiple of 16)"_s);
    } else if (std::max(structure.sizeX, structure.sizeZ) * 2 > surface.spacing) {
        reader.fail(u"'spacing' must be at least twice the structure's width"_s);
    }
    return surface;
}

// A corner of a guardian zone: a cell inside the structure.
std::optional<QcBlockPos> readCorner(const QJsonValue &value, const Structure &structure)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return std::nullopt;
    }
    const QcBlockPos cell{array[0].toInt(-1), array[1].toInt(-1), array[2].toInt(-1)};
    const bool inside = cell.x >= 0 && cell.y >= 0 && cell.z >= 0 && cell.x < structure.sizeX && cell.y < structure.sizeY && cell.z < structure.sizeZ;
    return inside ? std::optional<QcBlockPos>(cell) : std::nullopt;
}

[[nodiscard]] std::optional<Structure::Guardian> parseGuardian(const QJsonObject &object, const Structure &structure, QcJsonFieldReader &reader,
                                                               const QString &packNamespace)
{
    Structure::Guardian guardian;
    guardian.mob = reader.name(u"mob"_s, packNamespace);
    const std::optional<QcBlockPos> from = readCorner(object.value(u"from"_s), structure);
    const std::optional<QcBlockPos> to = readCorner(object.value(u"to"_s), structure);
    if (guardian.mob.isEmpty() || !from || !to) {
        reader.fail(u"needs a 'mob' and the corners 'from' and 'to' inside the structure"_s);
        return std::nullopt;
    }
    guardian.from = {std::min(from->x, to->x), std::min(from->y, to->y), std::min(from->z, to->z)};
    guardian.to = {std::max(from->x, to->x), std::max(from->y, to->y), std::max(from->z, to->z)};
    guardian.max = reader.integer(u"max"_s, guardian.max, 1, 16);
    guardian.cooldownSeconds = reader.number(u"cooldownSeconds"_s, guardian.cooldownSeconds, 1.0, 86400.0);
    guardian.minPlayerDistance = reader.number(u"minPlayerDistance"_s, guardian.minPlayerDistance, 0.0, 128.0);
    guardian.maxPlayerDistance = reader.number(u"maxPlayerDistance"_s, guardian.maxPlayerDistance, 1.0, 128.0);
    guardian.maxLight = reader.integer(u"maxLight"_s, guardian.maxLight, 0, 15);
    if (guardian.minPlayerDistance >= guardian.maxPlayerDistance) {
        reader.fail(u"needs 'minPlayerDistance' < 'maxPlayerDistance'"_s);
        return std::nullopt;
    }
    return guardian;
}

[[nodiscard]] QList<Structure::Guardian> parseGuardians(const QJsonObject &json, const Structure &structure, QcJsonFieldReader &reader,
                                                        const QString &packNamespace)
{
    QList<Structure::Guardian> result;
    const QJsonValue value = json.value(u"guardians"_s);
    if (value.isUndefined()) {
        return result;
    }
    if (!value.isArray() || value.toArray().size() > maxGuardians) {
        reader.fail(u"'guardians' must be an array of at most %1 zones"_s.arg(maxGuardians));
        return result;
    }
    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        QcJsonFieldReader nested{entry.toObject(), reader.context + u" guardians"_s, {}};
        if (!entry.isObject()) {
            nested.fail(u"entries must be objects"_s);
        } else if (const std::optional<Structure::Guardian> guardian = parseGuardian(entry.toObject(), structure, nested, packNamespace)) {
            result.append(*guardian);
        }
        reader.adopt(nested);
        if (!reader.ok()) {
            return result;
        }
    }
    return result;
}

constexpr int maxAreaMonsters = 8;

// The kinds of a spawn area (decision E-871): at least one and at most maxAreaMonsters names.
[[nodiscard]] Structure::SpawnArea parseSpawnArea(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    Structure::SpawnArea area;
    const QJsonValue value = json.value(u"monsters"_s);
    const QJsonArray entries = value.toArray();
    if (!value.isArray() || entries.isEmpty() || entries.size() > maxAreaMonsters) {
        reader.fail(u"needs 'monsters', an array of 1 to %1 creature names"_s.arg(maxAreaMonsters));
        return area;
    }
    for (const auto &entry : entries) {
        const QString mob = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (mob.isEmpty()) {
            reader.fail(u"'monsters' must be an array of creature names"_s);
            return area;
        }
        area.monsters.append(mob);
    }
    return area;
}

} // namespace

namespace QcStructureSiteParser {

Site parseSite(const QJsonObject &json, const QcStructureDefinition &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    Site site;
    site.groundLayer = reader.integer(u"groundLayer"_s, 0, 0, std::max(0, structure.sizeY - 1));
    reader.nestedObject(u"surface"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        site.surface = parseSurface(object, structure, nested, packNamespace);
    });
    if (reader.ok()) {
        site.guardians = parseGuardians(json, structure, reader, packNamespace);
    }
    reader.nestedObject(u"spawnArea"_s, [&](const QJsonObject &object, QcJsonFieldReader &nested) {
        site.spawnArea = parseSpawnArea(object, nested, packNamespace);
    });
    return site;
}

} // namespace QcStructureSiteParser
