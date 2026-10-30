// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/*! \brief One spot of a piece the village state lists (decision E-403): a bed (its head) or a
    workstation, in the piece's local cells. */
struct QcVillageSpot
{
    /*! The cell, in the piece's local cells (in world coordinates in QcVillagePlan). */
    QcBlockPos at;
    /*! Empty for a bed, else the kind of work (QcVillageSettings::workstations). */
    QString kind;
};

/*! \brief A style resolved against the registry: its pieces as structures with the style's
    palette applied. */
struct QcVillageStyle
{
    /*! \brief One blueprint of a kind of building, with its variation (decision E-404). */
    struct Blueprint
    {
        /*! The structure with its choices and parts. */
        QcPieceVariation variation;
        /*! The relative chance against the other blueprints of the kind. */
        int weight = 1;
        /*! Beds and workstations inside the structure as written, in local cells. */
        QList<QcVillageSpot> spots;
        /*! The meeting point block (QcVillageSettings::bell) inside it, if any. */
        std::optional<QcBlockPos> bell;
        /*! Crop cells in it or its parts: the style's crops are planted there (decision E-406). */
        bool fields = false;

        /*! Returns the structure as written, without parts (QcPieceVariation::base()). */
        const std::shared_ptr<const QcStructure> &structure() const { return variation.base(); }
    };

    /*! \brief A kind of building (QcVillagePiece) with its blueprints and rules. */
    struct Piece
    {
        /*! The blueprints, one chosen by weight per building. */
        QList<Blueprint> blueprints;
        /*! The relative chance against the other kinds of the style. */
        int weight = 1;
        /*! At most this many in one village; 0: no limit (a kind with rules: at least one). */
        int max = 0;
        /*! Rule: the chance per village that the kind is built (for the rare centre: that it
            stands instead of the centre). */
        double chance = 1.0;
        /*! Rule: built only in villages whose roads are at least this many blocks long together. */
        int minRoadLength = 0;
        /*! Rule: built only beside a road end with water at most this many blocks ahead; 0: no
            water rule. */
        int water = 0;
        /*! The jetty of a piece with a water rule (QcVillageJetty): its deck and post blocks
            (through the style's palette), its odd width and how many cells it reaches out over
            the water. \qcshared */
        QcBlockId deck = qcAirBlockId;
        QcBlockId post = qcAirBlockId;
        int jettyWidth = 1;
        int jettyOver = 4;

        /*! Returns whether the kind has a rule: a chance below 1, a minimum road length or a
            water rule. */
        bool hasRules() const { return chance < 1.0 || minRoadLength > 0 || water > 0; }
        /*! Returns the first blueprint (the centre and the lamp have just one). */
        const Blueprint &first() const { return blueprints.front(); }
    };

    /*! \brief A road surface (QcVillageRoad): \c {block} goes into the ground's top block,
        \c {step} is the slab where the road steps up a block (air: none), \c {weight} the
        relative chance among the surfaces of its rank. \qcfields */
    struct Road
    {
        QcBlockId block = qcAirBlockId;
        QcBlockId step = qcAirBlockId;
        int weight = 1;
    };

    /*! \brief A crop of the fields (QcVillageCrop): its states in the order of its stages, the
        last ripe (\c {stages}), and its relative chance among the style's crops (\c {weight}).
        \qcfields */
    struct Crop
    {
        QList<QcBlockId> stages;
        int weight = 1;
    };

    /*! \brief The posts and stairs of a style over water (QcVillageStilts, decision E-407). */
    struct Stilts
    {
        /*! The block of the posts under pieces, boardwalks and stairs. */
        QcBlockId post = qcAirBlockId;
        /*! The stairs block's state facing north; turned per stair. */
        QcBlockId stair = qcAirBlockId;
        /*! Floors and boardwalks lie this many blocks above the water surface. */
        int height = 3;
        /*! Posts stand every this many cells. */
        int spacing = 3;
        /*! The ground lies at most this many blocks below the water surface where a piece or
            boardwalk stands. */
        int maxDepth = 5;
        /*! At least this share of the samples around a centre lies under water
            (QcBoardwalkPlanner::wetAround()). */
        double water = 0.25;
    };

    /*! \brief The ground a style over water shapes (QcVillageWetland); \c {bottom} air: none. */
    struct Wetland
    {
        /*! The block of the pools' bottoms, the mud and raised ground. */
        QcBlockId bottom = qcAirBlockId;
        /*! Pools lie this many blocks below the water surface (their deepest part a block
            more). */
        int depth = 1;
        /*! How far the wetland reaches from the pieces and boardwalks in blocks: \c {margin}, up
            to \c {fray} more where noise bends its edge, and \c {blend} over which the ground
            returns to the terrain; reach() is their sum. \qcshared */
        int margin = 4;
        int fray = 3;
        int blend = 4;
        /*! The pools on the wetland (decision E-661): their share of the open ground, the band
            of \c {bottom} around them, the noise scale in blocks, the plant beside them (air:
            none) and its chance per column, and the plant on the water of the pools (air: none)
            with its chance. \qcshared */
        double water = 0.4;
        int mudBand = 2;
        double scale = 8.0;
        QcBlockId plant = qcAirBlockId;
        double plantChance = 0.0;
        QcBlockId floatingPlant = qcAirBlockId;
        double floatingPlantChance = 0.0;

        /*! Returns the distance from the pieces and boardwalks beyond which columns keep their
            terrain. */
        int reach() const { return margin + fray + blend; }
    };

    /*! \brief A bridge carries a road of a style on land over a river (decision E-841): the deck
        one block above the water (\c {deck}) and the posts under it (\c {post}), oak planks and
        logs through the style's palette; air: no bridges. \qcfields */
    struct Bridge
    {
        QcBlockId deck = qcAirBlockId;
        QcBlockId post = qcAirBlockId;
    };

    /*! The style's name. */
    QString name;
    /*! Pieces and roads stand over water on stilts (decision E-407): floors and boardwalks at
        one height above the water surface instead of on the ground. */
    bool overWater = false;
    /*! Over water: the posts and stairs. */
    Stilts stilts;
    /*! Over water: the ground the village shapes around itself (\c {bottom} air: none). */
    Wetland wetland;
    /*! The centre piece, one blueprint. */
    Piece centre;
    /*! Taken instead of the centre with its \c {chance} (decision E-592); no blueprints: none. */
    Piece rareCentre;
    /*! No blueprints: no lamps. */
    Piece lamp;
    /*! The kinds of buildings. */
    QList<Piece> pieces;
    /*! The surfaces of the roads from the centre and of the side roads; never empty.
        \qcshared */
    QList<Road> roads;
    QList<Road> branchRoads;
    /*! Empty: the pieces keep their crops as written. */
    QList<Crop> crops;
    /*! Street plans by weight (decision E-408), never empty; over water without diagonals and
        winding. */
    QList<QcVillageLayout> layouts;
    /*! The block that fills the ground below the pieces. */
    QcBlockId foundation = qcAirBlockId;
    /*! On land: the bridges over rivers. */
    Bridge bridge;
};

/*! \brief The village styles of the content (decision E-403), resolved once per world generator:
    which style a biome builds in, with every piece's structure, palette swaps, beds and
    workstations.

    Immutable after construction, so the generator's threads share it. */
class QcVillageStyles
{
public:
    /*! Resolves the village styles of \a content. Styles with an unknown structure or block are
        left out with a warning (warnings()). */
    explicit QcVillageStyles(const QcContentRegistry &content);

    /*! Returns what resolving the styles left out or changed. */
    const QStringList &warnings() const { return m_warnings; }

    /*! Returns whether no style resolved. */
    bool isEmpty() const { return m_styles.isEmpty(); }
    /*! Returns the style of \a biome: the one that names it, else the fallback; null for biomes
        without villages or when no style fits. */
    const QcVillageStyle *forBiome(const QcBiomeDefinition &biome) const;
    /*! Returns the style called \a name; null when there is none. */
    const QcVillageStyle *named(const QString &name) const;
    /*! Returns the block states the village counts. */
    const QcVillageBlocks &blocks() const { return m_blocks; }

    /*! \brief The beds and workstations of a structure (\c {spots}), and its meeting point
        block (the \c {bell}) if it has one. \qcfields */
    struct Spots
    {
        QList<QcVillageSpot> spots;
        std::optional<QcBlockPos> bell;
    };
    /*! Returns the spots of \a structure, in its local cells (the last bell found if it has
        several). */
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
