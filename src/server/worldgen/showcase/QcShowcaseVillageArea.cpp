#include "QcShowcaseVillageArea.h"

#include "QcVillageStyles.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Blocks between two buildings of a row, and between a row's road and the next row.
constexpr int pieceGap = 2;
constexpr int rowGap = 3;

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

} // namespace

namespace QcShowcaseVillageArea {

bool hasStyles(const QcContentRegistry &content)
{
    return !content.villageStyles().isEmpty();
}

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    const QcVillageStyles styles(context.content, nullptr);
    QList<std::pair<const QcVillageStyle *, QList<std::shared_ptr<const QcStructure>>>> rows;
    int width = 1;
    int depth = 0;
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

    auto area = std::make_shared<QcStructure>(u"showcase/villages"_s, width, height, std::max(1, depth));
    // Rows from the south (the visitors' side) to the north; each road lies south of its row.
    int road = area->sizeZ() - 1;
    for (const auto &[style, pieces] : std::as_const(rows)) {
        for (int x = 0; x < width; ++x) {
            area->set(x, 0, road, style->roads.front().block);
        }
        int x = 1;
        for (const std::shared_ptr<const QcStructure> &piece : pieces) {
            copyPiece(*area, *piece, x, road - 1);
            area->labels.append({{x + piece->sizeX() / 2.0, double(piece->sizeY()) + 1.5, road + 0.5}, titleOf(*piece),
                                 style->name.mid(style->name.indexOf(u':') + 1), false});
            x += piece->sizeX() + pieceGap;
        }
        road -= rowDepth(pieces) + 1 + rowGap;
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - area->sizeZ() + 1};
    return {u"villages"_s, u"showcase.area.villages"_s, std::move(area), origin};
}

} // namespace QcShowcaseVillageArea
