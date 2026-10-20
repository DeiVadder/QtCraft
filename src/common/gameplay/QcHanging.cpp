#include "common/gameplay/QcHanging.h"

#include <algorithm>

namespace {

QcVec3 vectorOf(QcFaceOffset offset)
{
    return {double(offset.dx), double(offset.dy), double(offset.dz)};
}

} // namespace

QcFaceOffset QcHanging::rightOf(QcBlockFace face)
{
    // Up x normal for the side faces: looking at a north face (from the north, towards +Z) the right
    // is -X. Frames on the floor or the ceiling point their item north at rotation 0.
    switch (face) {
    case QcBlockFace::North:
        return {-1, 0, 0};
    case QcBlockFace::South:
        return {1, 0, 0};
    case QcBlockFace::East:
        return {0, 0, -1};
    case QcBlockFace::West:
        return {0, 0, 1};
    case QcBlockFace::Up:
    case QcBlockFace::Down:
        break;
    }
    return {1, 0, 0};
}

QList<QcBlockPos> QcHanging::cells() const
{
    const QcFaceOffset right = rightOf(face);
    QList<QcBlockPos> covered;
    covered.reserve(qsizetype(width) * height);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            covered.append(cell.offset(right.dx * column, row, right.dz * column));
        }
    }
    return covered;
}

QcBlockPos QcHanging::supportOf(QcBlockPos covered) const
{
    const QcFaceOffset normal = qcFaceOffset(face);
    return covered.offset(-normal.dx, -normal.dy, -normal.dz);
}

QcAabb QcHanging::box() const
{
    // The cells' bounds, cut down to a thin slab against the wall; a frame also shrinks to its side.
    const QList<QcBlockPos> covered = cells();
    QcVec3 low{double(covered.front().x), double(covered.front().y), double(covered.front().z)};
    QcVec3 high = low + QcVec3{1.0, 1.0, 1.0};
    for (const QcBlockPos pos : covered) {
        low = {std::min(low.x, double(pos.x)), std::min(low.y, double(pos.y)), std::min(low.z, double(pos.z))};
        high = {std::max(high.x, pos.x + 1.0), std::max(high.y, pos.y + 1.0), std::max(high.z, pos.z + 1.0)};
    }

    const QcVec3 normal = vectorOf(qcFaceOffset(face));
    const double depth = type == QcHangingType::ItemFrame ? frameDepth : paintingDepth;
    const double inset = type == QcHangingType::ItemFrame ? (1.0 - frameSide) * 0.5 : 0.0;
    const auto cut = [&](double lowValue, double highValue, double direction) -> std::pair<double, double> {
        if (direction > 0.0) {
            return {lowValue, lowValue + depth};
        }
        if (direction < 0.0) {
            return {highValue - depth, highValue};
        }
        return {lowValue + inset, highValue - inset};
    };
    const auto [x0, x1] = cut(low.x, high.x, normal.x);
    const auto [y0, y1] = cut(low.y, high.y, normal.y);
    const auto [z0, z1] = cut(low.z, high.z, normal.z);
    return {{x0, y0, z0}, {x1, y1, z1}};
}

QcVec3 QcHanging::centre() const
{
    const QcAabb slab = box();
    const QcVec3 middle = (slab.min + slab.max) * 0.5;
    return middle + vectorOf(qcFaceOffset(face)) * ((type == QcHangingType::ItemFrame ? frameDepth : paintingDepth) * 0.5);
}
