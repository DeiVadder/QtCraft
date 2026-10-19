#pragma once

#include "worldgen/villages/QcVillageDraft.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>

#include <optional>
#include <utility>

class QcContentRegistry;
class QcTerrainGenerator;

// The buildings of a village (decision E-403, E-404, E-419): the centre piece facing the main road,
// the kinds with rules where their rules hold (a kind with a water rule beside a road end with its
// jetty), pieces of the style by weight on both sides of the roads facing them (beside diagonal
// roads with a forecourt to the road), their doorsteps, and lamps along the roads. Deterministic
// and thread-safe (const).
class QcPiecePlacer
{
public:
    using Road = QcVillageDraft::Road;

    // Pieces of one village at most (the centre and lamps not counted).
    static constexpr int maxPieces = 28;

    QcPiecePlacer(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles);

    // The centre piece (the style's rare centre with its chance, decision E-592), its door towards
    // the main road; none where the ground does not fit.
    void placeCentre(QcVillageDraft &draft) const;
    // The kinds with rules (decision E-404, E-408) whose rules hold, each up to its maximum (at least one).
    void placeRulePieces(QcVillageDraft &draft, const QList<Road> &roads) const;
    // Pieces by weight along both sides of the road, until none is left to place.
    void placeAlong(QcVillageDraft &draft, const Road &road) const;
    // Lamps every lampSpacing blocks along the whole road, sides taking turns.
    void placeLamps(QcVillageDraft &draft, const Road &road) const;

private:
    using Rect = QcVillageDraft::Rect;

    // On land pieces start one block of grass beyond the road's edge.
    static constexpr int pieceOffset = QcVillageDraft::roadHalfWidth + 2;

    // Floor height for a piece over `rect` (the ground's top block), or std::nullopt where the
    // ground is too steep or wet; over water the floor height of the stilts (decision E-407), or
    // std::nullopt where the ground cannot carry them.
    [[nodiscard]] std::optional<int> floorFor(const QcVillageDraft &draft, Rect rect, int maxSlope) const;
    // The centre piece of the blueprint, its door towards the main road; false where the ground does
    // not fit.
    bool placeCentre(QcVillageDraft &draft, const QcVillageStyle::Blueprint &centre) const;
    // A kind without a water rule on a random free spot along a road; false when none fits.
    bool placeAnywhere(QcVillageDraft &draft, const QList<Road> &roads, int index) const;
    // A kind with a water rule beside the end of a road with water ahead, and its jetty from the
    // road end out over the water; false when no road end fits.
    bool placeAtWater(QcVillageDraft &draft, const QList<Road> &roads, int index) const;
    // A building of kind `index` beside the last blocks of the road; false when none fits.
    bool placeBesideEnd(QcVillageDraft &draft, const Road &road, int index) const;
    // Places a piece `along` the road on `side` (-1 left, 1 right); returns how far along it reaches,
    // 0 when it did not fit and -1 when no piece is left to place.
    int tryPiece(QcVillageDraft &draft, const Road &road, int along, int side) const;
    // Places a building of kind `index` like tryPiece(); returns how far along it reaches, 0 when it
    // did not fit.
    int placeAt(QcVillageDraft &draft, const Road &road, std::pair<int, int> alongAndSide, int index) const;
    // placeAt() beside a diagonal run: the door faces the road along the nearer axis, and a forecourt
    // of side road (QcVillageForecourt) leads from the door to the road.
    int placeAtDiagonal(QcVillageDraft &draft, const Road &road, std::pair<int, int> alongAndSide, int index) const;
    // A kind without rules chosen by weight among those not at their maximum; -1 when none is left.
    static int choosePiece(QcVillageDraft &draft);
    // A blueprint of the kind chosen by weight.
    static const QcVillageStyle::Blueprint &chooseBlueprint(QcVillageDraft &draft, const QcVillageStyle::Piece &piece);
    // Adds a building of the blueprint with its choices and parts drawn, its beds, workstations and
    // creatures, on land with the doorsteps of its outer doors (QcVillagePlan::thresholds); the placed piece,
    // valid until the next piece is added.
    [[nodiscard]] const QcStructurePlacement &addPiece(QcVillageDraft &draft, const QcVillageStyle::Blueprint &blueprint, Rect rect, QcBlockPos origin,
                                         int turns) const;
    // The cells in front of the outer doors of a placed piece, at the height of its floor.
    QList<QcBlockPos> doorsteps(const QcStructurePlacement &piece) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    const QcVillageStyles &m_styles;
    const QcVillageSettings &m_settings;
};
