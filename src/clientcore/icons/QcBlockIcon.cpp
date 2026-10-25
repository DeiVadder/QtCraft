#include "clientcore/icons/QcBlockIcon.h"

#include "clientcore/meshing/QcChunkMesher.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QcBlockIcon {

namespace {

using Quad = QcBlockRenderTable::Quad;

bool visible(QcBlockFace face)
{
    return face == QcBlockFace::Up || face == QcBlockFace::East || face == QcBlockFace::South;
}

QPointF project(std::array<float, 3> p, int size)
{
    // Seen from above the south-east corner: +x goes right and down, +z left and down.
    const double scale = size * 0.42;
    const double sx = (static_cast<double>(p[0]) - static_cast<double>(p[2])) * 0.866;
    const double sy = (static_cast<double>(p[0]) + static_cast<double>(p[2])) * 0.5 - static_cast<double>(p[1]);
    return {size * 0.5 + sx * scale, size * 0.5 + sy * scale};
}

// Multiplies the colour channels of an RGBA8888 image by `shade`.
void darken(QImage &image, float shade)
{
    for (int y = 0; y < image.height(); ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                line[x * 4 + channel] = static_cast<uchar>(static_cast<float>(line[x * 4 + channel]) * shade);
            }
        }
    }
}

// How the cells of the parts map into one cell: p' = p * scale + shift per axis.
struct Fit
{
    float scale = 1.0f;
    std::array<float, 3> shift{};
};

// The fit of quads (in cells) into one cell: the bounds of their geometry, scaled evenly so that
// their picture takes as much of the icon as a full cube's, centred where the cube's centre is.
Fit fitOf(const QList<Quad> &quads)
{
    std::array<float, 3> low{1.0f, 1.0f, 1.0f};
    std::array<float, 3> high{0.0f, 0.0f, 0.0f};
    for (const Quad &quad : quads) {
        for (const std::array<float, 3> &corner : quad.corners) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], corner[axis]);
                high[axis] = std::max(high[axis], corner[axis]);
            }
        }
    }
    if (quads.isEmpty()) {
        return {};
    }

    // The projection (project) of a box dx * dy * dz is (dx + dz) * 0.866 wide and
    // (dx + dz) * 0.5 + dy high; a unit cube's is 2 * 0.866 by 2, so the width fits at 2 / (dx + dz).
    const float dx = high[0] - low[0];
    const float dy = high[1] - low[1];
    const float dz = high[2] - low[2];
    const float across = dx + dz;
    const float height = across * 0.5f + dy;
    Fit fit;
    fit.scale = std::min(across > 0.0f ? 2.0f / across : 1.0f, height > 0.0f ? 2.0f / height : 1.0f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        fit.shift[axis] = 0.5f - (low[axis] + high[axis]) * 0.5f * fit.scale;
    }
    return fit;
}

// `quad` moved `offset` cells.
Quad moved(Quad quad, std::array<int, 3> offset)
{
    for (std::array<float, 3> &corner : quad.corners) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            corner[axis] += static_cast<float>(offset[axis]);
        }
    }
    return quad;
}

// The quads of all parts (a part without an entry draws nothing), moved to their cells and, when
// there are several parts, fitted into one cell (fitOf).
QList<Quad> placedQuads(const QList<Part> &parts)
{
    QList<Quad> quads;
    for (const Part &part : parts) {
        if (!part.entry) {
            continue;
        }
        const QList<Quad> own = QcBlockRenderTable::drawnQuads(*part.entry);
        for (const Quad &quad : own) {
            quads.append(moved(quad, part.offset));
        }
    }
    if (parts.size() < 2) {
        return quads;
    }

    const Fit fit = fitOf(quads);
    for (Quad &quad : quads) {
        for (std::array<float, 3> &corner : quad.corners) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                corner[axis] = corner[axis] * fit.scale + fit.shift[axis];
            }
        }
    }
    return quads;
}

// Draws one visible quad of an icon `size` pixels large, its texture mapped from `atlas`.
void paintQuad(QPainter &painter, const Quad &quad, const QImage &atlas, int size)
{
    const QPointF s0 = project(quad.corners[0], size);
    const QPointF s1 = project(quad.corners[1], size);
    const QPointF s3 = project(quad.corners[3], size);
    const QPointF t0(quad.uvs[0][0] * atlas.width(), quad.uvs[0][1] * atlas.height());
    const QPointF t1(quad.uvs[1][0] * atlas.width(), quad.uvs[1][1] * atlas.height());
    const QPointF t3(quad.uvs[3][0] * atlas.width(), quad.uvs[3][1] * atlas.height());

    QTransform transform;
    if (!QTransform::quadToQuad(QPolygonF{t0, t1, t1 + t3 - t0, t3}, QPolygonF{s0, s1, s1 + s3 - s0, s3}, transform)) {
        return;
    }

    QPainterPath outline;
    outline.addPolygon(QPolygonF{s0, s1, s1 + s3 - s0, s3});
    outline.closeSubpath();
    painter.save();
    painter.setClipPath(outline);
    painter.setTransform(transform);
    const QRectF source = QRectF(t0, t1 + t3 - t0).normalized();
    painter.drawImage(source, atlas, source);
    painter.restore();
}

} // namespace

std::array<QImage, 6> shadedAtlases(const QImage &atlas)
{
    std::array<QImage, 6> shaded;
    for (const QcBlockFace face : qcAllBlockFaces) {
        QImage image = atlas.convertToFormat(QImage::Format_RGBA8888);
        if (visible(face)) {
            darken(image, QcChunkMesher::faceShade[static_cast<std::size_t>(face)]);
        }
        shaded[static_cast<std::size_t>(face)] = std::move(image);
    }
    return shaded;
}

QImage render(const QcBlockRenderTable::Entry &entry, const std::array<QImage, 6> &shadedAtlas, int size)
{
    return render(QList<Part>{Part{&entry, {0, 0, 0}}}, shadedAtlas, size);
}

QImage render(const QList<Part> &parts, const std::array<QImage, 6> &shadedAtlas, int size)
{
    QImage icon(size, size, QImage::Format_RGBA8888);
    icon.fill(Qt::transparent);

    QList<Quad> quads = placedQuads(parts);

    // Painter's order: faces further from the viewer first.
    const auto depth = [](const Quad &quad) {
        float sum = 0.0f;
        for (const auto &p : quad.corners) {
            sum += p[0] + p[1] + p[2];
        }
        return sum;
    };
    std::ranges::stable_sort(quads, {}, depth);

    QPainter painter(&icon);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Quad &quad : std::as_const(quads)) {
        if (visible(quad.face)) {
            paintQuad(painter, quad, shadedAtlas[static_cast<std::size_t>(quad.face)], size);
        }
    }

    return icon;
}

} // namespace QcBlockIcon
