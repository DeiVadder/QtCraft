#include "server/hanging/QcHangingRules.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/hanging/QcHangingManager.h"

#include <algorithm>

namespace {

// Whether the block at `pos` holds a frame or painting; nothing while it is not loaded.
std::optional<bool> isSolid(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block) {
        return std::nullopt;
    }
    const QcContentRegistry &content = world.content();
    return content.isValidBlock(*block) && content.block(*block).solid;
}

// Whether the cell at `pos` leaves room for a frame or painting; nothing while it is not loaded.
std::optional<bool> isFree(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block) {
        return std::nullopt;
    }
    const QcContentRegistry &content = world.content();
    return content.isValidBlock(*block) && content.state(*block).collision.isEmpty() && !content.block(*block).liquid;
}

} // namespace

namespace QcHangingRules {

[[nodiscard]] std::optional<bool> holds(const QcBlockAccess &world, const QcHanging &hanging)
{
    if (hanging.type == QcHangingType::Painting && !QcHanging::isWallFace(hanging.face)) {
        return false;
    }
    const QList<QcBlockPos> cells = hanging.cells();
    for (const QcBlockPos cell : cells) {
        const std::optional<bool> free = isFree(world, cell);
        const std::optional<bool> solid = isSolid(world, hanging.supportOf(cell));
        if (!free || !solid) {
            return std::nullopt;
        }
        if (!*free || !*solid) {
            return false;
        }
    }
    return true;
}

bool fits(const QcBlockAccess &world, const QcHangingManager &hangings, const QcHanging &hanging)
{
    return holds(world, hanging).value_or(false) && !overlaps(hangings, hanging);
}

bool overlaps(const QcHangingManager &hangings, const QcHanging &hanging)
{
    const QList<QcBlockPos> cells = hanging.cells();
    return std::ranges::any_of(cells, [&](QcBlockPos cell) { return hangings.at(cell, hanging.face) != nullptr; });
}

[[nodiscard]] std::optional<QcHanging> painting(const QcBlockAccess &world, const QcHangingManager &hangings,
                                                const QcHangingDefinition &definition, const QcHanging &placed, QRandomGenerator &random)
{
    // As in the reference: the largest motifs that fit, one of them at random.
    const QcBlockPos clicked = placed.cell;
    const QcFaceOffset right = QcHanging::rightOf(placed.face);
    QList<QcHanging> fitting;
    int largest = 0;
    for (qsizetype index = 0; index < definition.motifs.size(); ++index) {
        const QcPaintingMotif &motif = definition.motifs[index];
        QcHanging candidate = placed;
        candidate.type = QcHangingType::Painting;
        candidate.width = motif.width;
        candidate.height = motif.height;
        candidate.motif = static_cast<quint8>(index);
        const int left = (motif.width - 1) / 2;
        candidate.cell = clicked.offset(-right.dx * left, -(motif.height - 1) / 2, -right.dz * left);
        const int area = motif.width * motif.height;
        if (area < largest || !fits(world, hangings, candidate)) {
            continue;
        }
        if (area > largest) {
            fitting.clear();
            largest = area;
        }
        fitting.append(candidate);
    }

    if (fitting.isEmpty()) {
        return std::nullopt;
    }
    return fitting[static_cast<qsizetype>(random.bounded(static_cast<quint32>(fitting.size())))];
}

} // namespace QcHangingRules
