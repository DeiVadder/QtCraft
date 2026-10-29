// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/villages/QcVillageStyles.h"

#include "common/content/villages/QcVillageLayoutParser.h"
#include "worldgen/villages/QcVillageFields.h"
#include "worldgen/villages/QcVillagePlanner.h"

using namespace Qt::StringLiterals;

namespace {

// The structure definition with the style's block swaps: "qtcraft:oak_planks[axis=x]" becomes
// "qtcraft:spruce_planks[axis=x]" when the palette swaps oak planks for spruce planks.
QcStructureDefinition swapped(const QcStructureDefinition &definition, const QHash<QString, QString> &palette)
{
    QcStructureDefinition result = definition;
    for (QString &state : result.palette) {
        const qsizetype bracket = state.indexOf(u'[');
        const QString block = bracket < 0 ? state : state.left(bracket);
        const auto it = palette.constFind(block);
        if (it != palette.cend()) {
            state = *it + (bracket < 0 ? QString() : state.mid(bracket));
        }
    }
    return result;
}

// A kind with one blueprint and no rules (the centre and the lamp).
QcVillagePiece single(const QString &structure)
{
    QcVillagePiece piece;
    piece.blueprints.append({structure, 1});
    return piece;
}

} // namespace

// The road surfaces of one rank; unknown blocks are left out, unknown steps dropped, with a warning.
QList<QcVillageStyle::Road> QcVillageStyles::resolveRoads(const QList<QcVillageRoad> &entries, const QcContentRegistry &content, const QString &style)
{
    QList<QcVillageStyle::Road> roads;
    for (const QcVillageRoad &entry : entries) {
        const std::optional<QcBlockId> block = content.stateId(entry.block);
        const std::optional<QcBlockId> step = entry.step.isEmpty() ? std::optional<QcBlockId>(qcAirBlockId) : content.stateId(entry.step);
        if (!block || !step) {
            m_warnings.append(u"village style '%1': unknown road block '%2' or step '%3'"_s.arg(style, entry.block, entry.step));
        }
        if (block) {
            roads.append({*block, step.value_or(qcAirBlockId), entry.weight});
        }
    }
    return roads;
}

// The crops of the fields with their stages; blocks that are no crop are left out with a warning.
QList<QcVillageStyle::Crop> QcVillageStyles::resolveCrops(const QList<QcVillageCrop> &entries, const QcContentRegistry &content, const QString &style)
{
    QList<QcVillageStyle::Crop> crops;
    for (const QcVillageCrop &entry : entries) {
        const QcBlockId block = content.blockId(entry.block).value_or(qcAirBlockId);
        const QcBlockDefinition &definition = content.block(block);
        const QcBlockProperty *stages = definition.crop.isCrop() ? definition.property(definition.crop.property) : nullptr;
        if (!stages) {
            m_warnings.append(u"village style '%1': '%2' is no crop, left out"_s.arg(style, entry.block));
            continue;
        }

        QcVillageStyle::Crop crop;
        crop.weight = entry.weight;
        for (const QString &value : stages->values) {
            crop.stages.append(content.withProperty(block, definition.crop.property, value));
        }
        crops.append(std::move(crop));
    }
    return crops;
}

// The bridges of a style on land (decision E-841): oak planks and logs through its palette; none
// (air) without them in the content.
QcVillageStyle::Bridge QcVillageStyles::bridgeOf(const QcVillageStyleDefinition &definition, const QcContentRegistry &content)
{
    const QString planks = u"qtcraft:oak_planks"_s;
    const QString log = u"qtcraft:oak_log"_s;
    const std::optional<QcBlockId> deck = content.stateId(definition.palette.value(planks, planks));
    const std::optional<QcBlockId> post = content.stateId(definition.palette.value(log, log));
    if (!deck || !post) {
        return {};
    }
    return {*deck, *post};
}

// The posts and stairs of a style over water; std::nullopt (with a warning) when one is unknown or
// the stairs have no facing.
std::optional<QcVillageStyle::Stilts> QcVillageStyles::resolveStilts(const QcVillageStyleDefinition &definition, const QcContentRegistry &content)
{
    const QcVillageStilts &entry = definition.stilts;
    const std::optional<QcBlockId> post = content.stateId(entry.post);
    const std::optional<QcBlockId> stair = content.stateId(entry.stair);
    const bool turns = stair && content.block(*stair).property(u"facing"_s) != nullptr;
    if (!post || !turns) {
        m_warnings.append(u"village style '%1': unknown stilt post '%2' or stairs '%3', left out"_s.arg(definition.name, entry.post, entry.stair));
        return std::nullopt;
    }
    return QcVillageStyle::Stilts{*post, content.withProperty(*stair, u"facing"_s, u"north"_s), entry.height, entry.spacing, entry.maxDepth,
                                  entry.water};
}

// The wetland within the margin every village keeps to its region's edge: the fray, then the blend,
// then the margin shrink until it fits, with a warning.
QcVillageStyle::Wetland QcVillageStyles::fitWetland(QcVillageStyle::Wetland wetland, const QcVillageSettings &settings, const QString &style)
{
    const int room = QcVillagePlanner::freeMargin(settings);
    const int wanted = wetland.reach();
    while (wetland.reach() > room && wetland.fray > 0) {
        --wetland.fray;
    }
    while (wetland.reach() > room && wetland.blend > 1) {
        --wetland.blend;
    }
    while (wetland.reach() > room && wetland.margin > 0) {
        --wetland.margin;
    }
    if (wetland.reach() < wanted) {
        m_warnings.append(u"village style '%1': the wetland reaches %2 blocks, but villages of radius %3 keep only %4 to their region's edge; "
                         u"it reaches %5"_s.arg(style).arg(wanted).arg(settings.radius).arg(room).arg(wetland.reach()));
    }
    return wetland;
}

// A plant of the wetland by name; air for none and for an unknown one (a warning).
QcBlockId QcVillageStyles::wetlandPlant(const QcContentRegistry &content, const QString &style, const QString &name)
{
    const std::optional<QcBlockId> plant = name.isEmpty() ? std::optional<QcBlockId>(qcAirBlockId) : content.stateId(name);
    if (!plant) {
        m_warnings.append(u"village style '%1': unknown wetland plant '%2', none"_s.arg(style, name));
    }
    return plant.value_or(qcAirBlockId);
}

// The style's wetland with its blocks resolved, fitted into the region's margin; std::nullopt
// without one or with an unknown bottom block (a warning), an unknown plant is left out (a warning).
std::optional<QcVillageStyle::Wetland> QcVillageStyles::resolveWetland(const QcVillageStyleDefinition &definition, const QcContentRegistry &content)
{
    const QcVillageWetland &wetland = definition.wetland;
    if (!wetland.enabled) {
        return std::nullopt;
    }
    const std::optional<QcBlockId> bottom = content.stateId(wetland.bottom);
    if (!bottom) {
        m_warnings.append(u"village style '%1': unknown wetland bottom '%2', no wetland"_s.arg(definition.name, wetland.bottom));
        return std::nullopt;
    }

    QcVillageStyle::Wetland resolved;
    resolved.bottom = *bottom;
    resolved.depth = wetland.depth;
    resolved.margin = wetland.margin;
    resolved.fray = wetland.fray;
    resolved.blend = wetland.blend;
    resolved.water = wetland.water;
    resolved.mudBand = wetland.mudBand;
    resolved.scale = wetland.scale;
    resolved.plant = wetlandPlant(content, definition.name, wetland.plant);
    resolved.plantChance = wetland.plantChance;
    resolved.floatingPlant = wetlandPlant(content, definition.name, wetland.floatingPlant);
    resolved.floatingPlantChance = wetland.floatingPlantChance;
    return fitWetland(resolved, content.villageCatalog().settings(), definition.name);
}

QcVillageStyles::QcVillageStyles(const QcContentRegistry &content)
    : m_blocks(content)
    , m_warnings(m_blocks.warnings())
{
    for (const QcVillageStyleDefinition &definition : content.villageCatalog().styles()) {
        std::optional<QcVillageStyle> style = resolve(definition, content);
        if (!style) {
            continue;
        }
        const qsizetype index = m_styles.size();
        m_styles.append(std::move(*style));
        m_byName.insert(definition.name, index);
        for (const QString &biome : definition.biomes) {
            m_byBiome.insert(biome, index);
        }
        if (definition.fallback) {
            m_fallback = index;
        }
    }
}

const QcVillageStyle *QcVillageStyles::forBiome(const QcBiomeDefinition &biome) const
{
    if (!biome.villages) {
        return nullptr;
    }
    const qsizetype index = m_byBiome.value(biome.name, m_fallback);
    return index >= 0 ? &m_styles[index] : nullptr;
}

const QcVillageStyle *QcVillageStyles::named(const QString &name) const
{
    const auto it = m_byName.constFind(name);
    return it != m_byName.cend() ? &m_styles[*it] : nullptr;
}

std::optional<QcVillageStyle> QcVillageStyles::resolve(const QcVillageStyleDefinition &definition, const QcContentRegistry &content)
{
    QcVillageStyle style;
    style.name = definition.name;
    style.roads = resolveRoads(definition.roads, content, definition.name);
    style.branchRoads = resolveRoads(definition.branchRoads, content, definition.name);
    style.crops = resolveCrops(definition.crops, content, definition.name);
    const std::optional<QcBlockId> foundation = content.stateId(definition.foundationBlock);
    std::optional<QcVillageStyle::Piece> centre = piece(single(definition.centre), definition, content);
    if (style.roads.isEmpty() || style.branchRoads.isEmpty() || !foundation || !centre) {
        m_warnings.append(u"village style '%1': unknown path, foundation or centre, left out"_s.arg(definition.name));
        return std::nullopt;
    }
    style.foundation = *foundation;
    style.centre = std::move(*centre);
    if (!definition.rareCentre.isEmpty()) {
        if (std::optional<QcVillageStyle::Piece> rare = piece(single(definition.rareCentre), definition, content)) {
            style.rareCentre = std::move(*rare);
            style.rareCentre.chance = definition.rareCentreChance;
        }
    }
    style.layouts = definition.layouts.isEmpty() ? QcVillageLayoutParser::defaultLayouts() : definition.layouts;
    if (definition.ground == QcVillageStyleDefinition::Ground::Water) {
        std::optional<QcVillageStyle::Stilts> stilts = resolveStilts(definition, content);
        if (!stilts) {
            return std::nullopt;
        }
        style.overWater = true;
        style.stilts = *stilts;
        // Boardwalks run along the axes: a house's whole front lies on one (decision E-407).
        for (QcVillageLayout &layout : style.layouts) {
            layout.diagonals = false;
            layout.winding = 0.0;
        }
        if (std::optional<QcVillageStyle::Wetland> wetland = resolveWetland(definition, content)) {
            style.wetland = *wetland;
        }
    }

    if (!style.overWater) {
        style.bridge = bridgeOf(definition, content);
    }
    if (!definition.lamp.isEmpty()) {
        if (std::optional<QcVillageStyle::Piece> lamp = piece(single(definition.lamp), definition, content)) {
            style.lamp = std::move(*lamp);
        }
    }
    for (const QcVillagePiece &entry : definition.pieces) {
        if (std::optional<QcVillageStyle::Piece> resolved = piece(entry, definition, content)) {
            style.pieces.append(std::move(*resolved));
        }
    }
    return style;
}

std::optional<QcVillageStyle::Piece> QcVillageStyles::piece(const QcVillagePiece &entry, const QcVillageStyleDefinition &style,
                                                            const QcContentRegistry &content)
{
    QcVillageStyle::Piece piece;
    for (const QcVillageBlueprint &blueprint : entry.blueprints) {
        if (std::optional<QcVillageStyle::Blueprint> resolved = this->blueprint(blueprint.structure, style, content)) {
            resolved->weight = blueprint.weight;
            piece.blueprints.append(std::move(*resolved));
        }
    }
    if (piece.blueprints.isEmpty()) {
        return std::nullopt;
    }

    piece.weight = entry.weight;
    piece.max = entry.max;
    piece.chance = entry.chance;
    piece.minRoadLength = entry.minRoadLength;
    piece.water = entry.water;
    if (entry.water > 0) {
        // The jetty takes the style's palette like the pieces do.
        const std::optional<QcBlockId> deck = content.stateId(style.palette.value(entry.jetty.deck, entry.jetty.deck));
        const std::optional<QcBlockId> post = content.stateId(style.palette.value(entry.jetty.post, entry.jetty.post));
        if (!deck || !post) {
            m_warnings.append(u"village style '%1': unknown jetty block, piece left out"_s.arg(style.name));
            return std::nullopt;
        }
        piece.deck = *deck;
        piece.post = *post;
        piece.jettyWidth = entry.jetty.width;
        piece.jettyOver = entry.jetty.over;
    }
    return piece;
}

std::optional<QcVillageStyle::Blueprint> QcVillageStyles::blueprint(const QString &name, const QcVillageStyleDefinition &style,
                                                                    const QcContentRegistry &content)
{
    const QcStructureDefinition *definition = content.worldgen().structure(name);
    if (!definition) {
        m_warnings.append(u"village style '%1': unknown structure '%2'"_s.arg(style.name, name));
        return std::nullopt;
    }

    // A village writes the blocks of its pieces, spawns their creatures, fills their loot chests
    // and names their guardian zones (decision E-590); the other extras of a structure need the
    // server when the chunk is first seen, which villages do not have.
    const bool extras = !definition->stands.isEmpty() || !definition->contents.isEmpty() || !definition->labels.isEmpty()
                        || !definition->updates.isEmpty() || !definition->growing.isEmpty() || !definition->minecarts.isEmpty()
                        || !definition->spawners.isEmpty();
    if (extras) {
        m_warnings.append(u"village style '%1': structure '%2' has contents, armour stands, labels, minecarts, updates or growing "
                         u"saplings, which villages leave out"_s.arg(style.name, name));
    }

    QcPieceVariation::Resolved variation = QcPieceVariation::resolve(swapped(*definition, style.palette), content);
    m_warnings.append(QcPieceVariation::warningsOf(variation));
    if (!variation) {
        m_warnings.append(u"village style '%1': %2"_s.arg(style.name, variation.error().error));
        return std::nullopt;
    }

    QcVillageStyle::Blueprint blueprint{std::move(*variation), 1, {}, std::nullopt, false};
    Spots spots = spotsOf(*blueprint.structure());
    blueprint.spots = std::move(spots.spots);
    blueprint.bell = spots.bell;
    blueprint.fields = QcVillageFields::hasCrops(*blueprint.variation.build(blueprint.variation.everything()), content);
    return blueprint;
}

QcVillageStyles::Spots QcVillageStyles::spotsOf(const QcStructure &structure) const
{
    Spots found;
    QList<QcVillageSpot> &spots = found.spots;
    const int layer = structure.sizeX() * structure.sizeZ();
    for (int cell = 0; cell < layer * structure.sizeY(); ++cell) {
        const QcBlockPos at{cell % structure.sizeX(), cell / layer, cell % layer / structure.sizeX()};
        const std::optional<QcBlockId> block = structure.block(at.x, at.y, at.z);
        if (!block) {
            continue;
        }
        if (m_blocks.isBell(*block)) {
            found.bell = at;
        } else if (m_blocks.isBedHead(*block)) {
            spots.append(QcVillageSpot{at, {}});
        } else if (const QString kind = m_blocks.workstationKind(*block); !kind.isEmpty()) {
            spots.append(QcVillageSpot{at, kind});
        }
    }
    return found;
}
