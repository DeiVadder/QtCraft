#include "clientcore/icons/QcBlockIcon.h"

#include "clientcore/meshing/QcChunkMesher.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QcBlockIcon {

namespace {

using Quad = QcBlockRenderTable::Quad;

// Corners of the full cube faces in the order the mesher uses (see QcBlockRenderTable).
constexpr std::array<std::array<std::array<int, 3>, 4>, 6> faceCorners = {{
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
}};

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
    QImage icon(size, size, QImage::Format_RGBA8888);
    icon.fill(Qt::transparent);

    QList<Quad> quads;
    if (entry.fullCube) {
        for (const QcBlockFace face : qcAllBlockFaces) {
            const QRectF &uv = entry.faceUv[static_cast<std::size_t>(face)];
            Quad quad;
            quad.face = face;
            for (std::size_t corner = 0; corner < 4; ++corner) {
                const auto &c = faceCorners[static_cast<std::size_t>(face)][corner];
                quad.corners[corner] = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
                const bool left = corner == 0 || corner == 3;
                quad.uvs[corner] = {static_cast<float>(left ? uv.left() : uv.right()), static_cast<float>(corner < 2 ? uv.bottom() : uv.top())};
            }
            quads.append(quad);
        }
    } else {
        quads = entry.quads;
    }

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
        if (!visible(quad.face)) {
            continue;
        }

        const QImage &atlas = shadedAtlas[static_cast<std::size_t>(quad.face)];
        const QPointF s0 = project(quad.corners[0], size);
        const QPointF s1 = project(quad.corners[1], size);
        const QPointF s3 = project(quad.corners[3], size);
        const QPointF t0(quad.uvs[0][0] * atlas.width(), quad.uvs[0][1] * atlas.height());
        const QPointF t1(quad.uvs[1][0] * atlas.width(), quad.uvs[1][1] * atlas.height());
        const QPointF t3(quad.uvs[3][0] * atlas.width(), quad.uvs[3][1] * atlas.height());

        QTransform transform;
        if (!QTransform::quadToQuad(QPolygonF{t0, t1, t1 + t3 - t0, t3}, QPolygonF{s0, s1, s1 + s3 - s0, s3}, transform)) {
            continue;
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

    return icon;
}

} // namespace QcBlockIcon
