#include "worldgen/showcase/QcShowcaseVillageArea.h"

#include "worldgen/showcase/QcShowcaseSettlement.h"
#include "worldgen/villages/QcVillageFields.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Blocks between two buildings of a row, and between a row's road and the next row: each row is a
// village of its own (decision E-411), and a village lists the beds and workstations up to
// QcVillagePlan::reach around its area, so the rows lie further apart than that from each other.
constexpr int pieceGap = 2;
constexpr int rowGap = 2 * QcVillagePlan::reach + 1;
// Ground in front of the first row: the areas south of the village area lie beyond its first
// village's reach (their podiums hold beds and workstations).
constexpr int frontGap = QcVillagePlan::reach + 1;

// The buildings of a style in the order they are shown: well, lamp post, every blueprint of every
// kind with all its optional parts (decision E-404).
QList<std::shared_ptr<const QcStructure>> piecesOf(const QcVillageStyle &style)
{
    QList<std::shared_ptr<const QcStructure>> pieces{style.centre.first().structure()};
    if (!style.lamp.blueprints.isEmpty()) {
        pieces.append(style.lamp.first().structure());
    }
    for (const QcVillageStyle::Piece &piece : style.pieces) {
        for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
            pieces.append(blueprint.variation.build(blueprint.variation.everything()));
        }
    }
    return pieces;
}

int rowWidth(const QList<std::shared_ptr<const QcStructure>> &pieces)
{
    int width = 0;
    for (const std::shared_ptr<const QcStructure> &piece : pieces) {
        width += piece->sizeX() + pieceGap;
    }
    return width;
}

int rowDepth(const QList<std::shared_ptr<const QcStructure>> &pieces)
{
    int depth = 1;
    for (const std::shared_ptr<const QcStructure> &piece : pieces) {
        depth = std::max(depth, piece->sizeZ());
    }
    return depth;
}

// "qtcraft:village_plains_well" -> "structure.qtcraft.village_plains_well".
QString titleOf(const QcStructure &structure)
{
    QString key = structure.name();
    return u"structure."_s + key.replace(u':', u'.');
}

// Copies a building into the area with its south edge on `front` (local z), from local x `x`.
void copyPiece(QcStructure &area, const QcStructure &piece, int x, int front)
{
    const int z0 = front - piece.sizeZ() + 1;
    const int layer = piece.sizeX() * piece.sizeZ();
    for (int cell = 0; cell < layer * piece.sizeY(); ++cell) {
        const int dx = cell % piece.sizeX();
        const int y = cell / layer;
        const int z = cell % layer / piece.sizeX();
        if (const std::optional<QcBlockId> block = piece.block(dx, y, z)) {
            area.set(x + dx, y, z0 + z, *block);
        }
    }
}

// The village of a row in world coordinates (`origin`: the area's): its road, its bounds from the
// road to the deepest building, and the well's bell (else the well's ground) as the meeting point.
// Its beds and workstations come from scanning the chunks; residents move in even though creatures
// do not spawn in the showcase (QcVillagePlan::alwaysLivedIn), and they are confined so the row stays
// as built (QcVillagePlan::residentsConfined, decision E-510).
// Where a row lies in the area: its width, the local z of its road and its depth north of the road.
struct RowSpan
{
    int width = 0;
    int road = 0;
    int depth = 0;
};

std::shared_ptr<QcVillagePlan> planOf(const QcVillageStyle &style, qint64 id, QcBlockPos origin, RowSpan row)
{
    const int width = row.width;
    const int road = row.road;
    auto plan = std::make_shared<QcVillagePlan>();
    plan->id = id;
    plan->style = style.name;
    plan->alwaysLivedIn = true;
    plan->residentsConfined = true;
    plan->minX = origin.x;
    plan->maxX = origin.x + width - 1;
    plan->minZ = origin.z + road - row.depth;
    plan->maxZ = origin.z + road;
    for (int x = 0; x < width; ++x) {
        plan->roads.append({origin.x + x, origin.y, origin.z + road});
    }

    // The well is the first piece, from local x 1 with its south edge on the row's front.
    const QcVillageStyle::Blueprint &well = style.centre.first();
    const QcBlockPos wellOrigin{origin.x + 1, origin.y, origin.z + road - well.structure()->sizeZ()};
    plan->centre = well.bell ? wellOrigin.offset(well.bell->x, well.bell->y, well.bell->z)
                             : wellOrigin.offset(well.structure()->sizeX() / 2, 1, well.structure()->sizeZ() / 2);
    return plan;
}

} // namespace

namespace QcShowcaseVillageArea {

bool hasStyles(const QcContentRegistry &content)
{
    return !content.villageStyles().isEmpty();
}

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    const QcVillageStyles styles(context.content);
    QList<std::pair<const QcVillageStyle *, QList<std::shared_ptr<const QcStructure>>>> rows;
    int width = 1;
    int depth = frontGap;
    int height = 2;
    for (const QcVillageStyleDefinition &definition : context.content.villageStyles()) {
        const QcVillageStyle *style = styles.named(definition.name);
        if (!style) {
            continue;
        }
        QList<std::shared_ptr<const QcStructure>> pieces = piecesOf(*style);
        width = std::max(width, rowWidth(pieces));
        depth += rowDepth(pieces) + 1 + rowGap;
        for (const std::shared_ptr<const QcStructure> &piece : std::as_const(pieces)) {
            height = std::max(height, piece->sizeY() + 3);
        }
        rows.append({style, std::move(pieces)});
    }

    // One row of ground behind the last row instead of a full gap.
    depth -= rows.isEmpty() ? 0 : rowGap - 1;

    // The village station (decision E-490) east of the rows.
    QcShowcaseSettlement::Frame station{width, std::max(1, depth) - 1 - frontGap, QcShowcaseSettlement::settlementX(width, anchor.x), 0};
    auto area = std::make_shared<QcStructure>(u"showcase/villages"_s, QcShowcaseSettlement::areaWidth(station.x), height, std::max(1, depth));
    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - area->sizeZ() + 1};
    QList<std::shared_ptr<const QcVillagePlan>> villages;
    // Rows from the south (the visitors' side) to the north; each road lies south of its row.
    int road = area->sizeZ() - 1 - frontGap;
    for (const auto &[style, pieces] : std::as_const(rows)) {
        const std::shared_ptr<QcVillagePlan> plan = planOf(*style, villages.size() + 1, origin, {width, road, rowDepth(pieces)});
        for (int x = 0; x < width; ++x) {
            area->set(x, 0, road, style->roads.front().block);
        }
        int x = 1;
        for (const std::shared_ptr<const QcStructure> &piece : pieces) {
            copyPiece(*area, *piece, x, road - 1);
            const QcBlockPos pieceOrigin = origin.offset(x, 0, road - piece->sizeZ());
            const QList<QcBlockPos> crops = QcVillageFields::cropCells(*piece, context.content);
            for (const QcBlockPos &cell : crops) {
                plan->fields.insert(pieceOrigin.offset(cell.x, cell.y, cell.z));
            }
            area->labels.append({{x + piece->sizeX() / 2.0, double(piece->sizeY()) + 1.5, road + 0.5}, titleOf(*piece),
                                 style->name.mid(style->name.indexOf(u':') + 1), false});
            x += piece->sizeX() + pieceGap;
        }
        villages.append(plan);
        road -= rowDepth(pieces) + 1 + rowGap;
    }

    QList<QcBlockPos> points;
    const std::optional<int> stationZ = QcShowcaseSettlement::settlementZ(area->sizeZ(), origin.z, frontGap);
    if (!rows.isEmpty() && stationZ) {
        station.z = *stationZ;
        points = QcShowcaseSettlement::build(context, *area, origin, station);
    }
    return {u"villages"_s, u"showcase.area.villages"_s, std::move(area), origin, std::move(villages), std::move(points)};
}

} // namespace QcShowcaseVillageArea
