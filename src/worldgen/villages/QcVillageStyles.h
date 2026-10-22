#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/structures/QcStructure.h"
#include "worldgen/villages/QcPieceVariation.h"
#include "worldgen/villages/QcVillageBlocks.h"

#include <QHash>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

// One spot of a piece the village state lists (decision E-403): a bed (its head) or a workstation,
// in the piece's local cells.
struct QcVillageSpot
{
    QcBlockPos at;
    // Empty for a bed, else the kind of work (QcVillageSettings::workstations).
    QString kind;
};

// A style resolved against the registry: its pieces as structures with the style's palette applied.
struct QcVillageStyle
{
    // One blueprint of a kind of building, with its variation (decision E-404).
    struct Blueprint
    {
        QcPieceVariation variation;
        int weight = 1;
        // Beds and workstations inside the structure as written, in local cells.
        QList<QcVillageSpot> spots;
        // The meeting point block (QcVillageSettings::bell) inside it, if any.
        std::optional<QcBlockPos> bell;
        // Crop cells in it or its parts: the style's crops are planted there (decision E-406).
        bool fields = false;

        const std::shared_ptr<const QcStructure> &structure() const { return variation.base(); }
    };

    // A kind of building (QcVillagePiece) with its blueprints and rules.
    struct Piece
    {
        QList<Blueprint> blueprints;
        int weight = 1;
        int max = 0;
        double chance = 1.0;
        int minRoadLength = 0;
        int water = 0;
        // The jetty of a piece with a water rule (QcVillageJetty).
        QcBlockId deck = qcAirBlockId;
        QcBlockId post = qcAirBlockId;
        int jettyWidth = 1;
        int jettyOver = 4;

        bool hasRules() const { return chance < 1.0 || minRoadLength > 0 || water > 0; }
        // The first blueprint (the centre and the lamp have just one).
        const Blueprint &first() const { return blueprints.front(); }
    };

    // A road surface (QcVillageRoad); `step` is air for none.
    struct Road
    {
        QcBlockId block = qcAirBlockId;
        QcBlockId step = qcAirBlockId;
        int weight = 1;
    };

    // A crop of the fields (QcVillageCrop): its states in the order of its stages, the last ripe.
    struct Crop
    {
        QList<QcBlockId> stages;
        int weight = 1;
    };

    // The posts and stairs of a style over water (QcVillageStilts, decision E-407).
    struct Stilts
    {
        QcBlockId post = qcAirBlockId;
        // The stairs block's state facing north; turned per stair.
        QcBlockId stair = qcAirBlockId;
        int height = 3;
        int spacing = 3;
        int maxDepth = 5;
        double water = 0.25;
    };

    // The ground a style over water shapes (QcVillageWetland); `bottom` air: none.
    struct Wetland
    {
        QcBlockId bottom = qcAirBlockId;
        int depth = 1;
        int margin = 4;
        int fray = 3;
        int blend = 4;
        // The pools on the wetland (decision E-661): their share of the open ground, the band of
        // `bottom` around them, the noise scale in blocks, the plant beside them (air: none) and its
        // chance per column, and the plant on the water of the pools (air: none) with its chance.
        double water = 0.4;
        int mudBand = 2;
        double scale = 8.0;
        QcBlockId plant = qcAirBlockId;
        double plantChance = 0.0;
        QcBlockId floatingPlant = qcAirBlockId;
        double floatingPlantChance = 0.0;

        // Columns farther than this from the pieces and boardwalks keep their terrain.
        int reach() const { return margin + fray + blend; }
    };

    // A bridge carries a road of a style on land over a river (decision E-841): the deck one block
    // above the water and the posts under it, oak planks and logs through the style's palette; air:
    // no bridges.
    struct Bridge
    {
        QcBlockId deck = qcAirBlockId;
        QcBlockId post = qcAirBlockId;
    };

    QString name;
    // Pieces and roads stand over water on stilts (decision E-407): floors and boardwalks at one
    // height above the water surface instead of on the ground.
    bool overWater = false;
    Stilts stilts;
    Wetland wetland;
    Piece centre;
    // Taken instead of the centre with its `chance` (decision E-592); no blueprints: none.
    Piece rareCentre;
    // No blueprints: no lamps.
    Piece lamp;
    QList<Piece> pieces;
    // Never empty.
    QList<Road> roads;
    QList<Road> branchRoads;
    // Empty: the pieces keep their crops as written.
    QList<Crop> crops;
    // Street plans by weight (decision E-408), never empty; over water without diagonals and winding.
    QList<QcVillageLayout> layouts;
    QcBlockId foundation = qcAirBlockId;
    Bridge bridge;
};

// The village styles of the content (decision E-403), resolved once per world generator: which
// style a biome builds in, with every piece's structure, palette swaps, beds and workstations.
// Immutable after construction, so the generator's threads share it.
class QcVillageStyles
{
public:
    // Styles with an unknown structure or block are left out with a warning (warnings()).
    explicit QcVillageStyles(const QcContentRegistry &content);

    // What resolving the styles left out or changed.
    const QStringList &warnings() const { return m_warnings; }

    bool isEmpty() const { return m_styles.isEmpty(); }
    // The style of a biome: the one that names it, else the fallback; null for biomes without
    // villages or when no style fits.
    const QcVillageStyle *forBiome(const QcBiomeDefinition &biome) const;
    const QcVillageStyle *named(const QString &name) const;
    const QcVillageBlocks &blocks() const { return m_blocks; }

    // The beds and workstations of a structure, and its meeting point block (the bell) if it has one.
    struct Spots
    {
        QList<QcVillageSpot> spots;
        std::optional<QcBlockPos> bell;
    };
    [[nodiscard]] Spots spotsOf(const QcStructure &structure) const;

private:
    // The parts of resolving a style; what they leave out goes to m_warnings.
    [[nodiscard]] std::optional<QcVillageStyle> resolve(const QcVillageStyleDefinition &definition, const QcContentRegistry &content);
    // A kind with its blueprints, rules and jetty; std::nullopt when no blueprint resolves.
    [[nodiscard]] std::optional<QcVillageStyle::Piece> piece(const QcVillagePiece &entry, const QcVillageStyleDefinition &style,
                                                             const QcContentRegistry &content);
    [[nodiscard]] std::optional<QcVillageStyle::Blueprint> blueprint(const QString &name, const QcVillageStyleDefinition &style,
                                                                     const QcContentRegistry &content);
    [[nodiscard]] QList<QcVillageStyle::Road> resolveRoads(const QList<QcVillageRoad> &entries, const QcContentRegistry &content,
                                                           const QString &style);
    [[nodiscard]] QList<QcVillageStyle::Crop> resolveCrops(const QList<QcVillageCrop> &entries, const QcContentRegistry &content,
                                                           const QString &style);
    [[nodiscard]] static QcVillageStyle::Bridge bridgeOf(const QcVillageStyleDefinition &definition, const QcContentRegistry &content);
    [[nodiscard]] std::optional<QcVillageStyle::Stilts> resolveStilts(const QcVillageStyleDefinition &definition, const QcContentRegistry &content);
    [[nodiscard]] QcVillageStyle::Wetland fitWetland(QcVillageStyle::Wetland wetland, const QcVillageSettings &settings, const QString &style);
    [[nodiscard]] QcBlockId wetlandPlant(const QcContentRegistry &content, const QString &style, const QString &name);
    [[nodiscard]] std::optional<QcVillageStyle::Wetland> resolveWetland(const QcVillageStyleDefinition &definition,
                                                                         const QcContentRegistry &content);

    QList<QcVillageStyle> m_styles;
    QHash<QString, qsizetype> m_byBiome;
    QHash<QString, qsizetype> m_byName;
    qsizetype m_fallback = -1;
    QcVillageBlocks m_blocks;
    QStringList m_warnings;
};
