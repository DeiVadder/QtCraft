#include "clientcore/icons/QcBlockIcon.h"

#include "clientcore/meshing/QcChunkMesher.h"

#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

// One straight-alpha RGBA8888 pixel.
using Rgba = std::array<uchar, 4>;

Rgba rgbaAt(const uchar *bytes)
{
    return {bytes[0], bytes[1], bytes[2], bytes[3]};
}

// A straight-alpha texel composited over a straight-alpha pixel, in integers.
Rgba blendOver(Rgba pixel, Rgba texel)
{
    const int alpha = texel[3];
    if (alpha == 0) {
        return pixel;
    }
    if (alpha == 255) {
        return texel;
    }

    const int below = pixel[3] * (255 - alpha);
    const int out = alpha * 255 + below;
    Rgba blended{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        blended[channel] = static_cast<uchar>((texel[channel] * alpha * 255 + pixel[channel] * below + out / 2) / out);
    }
    blended[3] = static_cast<uchar>((out + 127) / 255);
    return blended;
}

// Pixels of an icon `size` pixels large and texels of `atlas` that a quad covers: the screen
// parallelogram s0 + a * (s1 - s0) + b * (s3 - s0) and the texture one at the same a, b.
// The depth towards the viewer (x + y + z, larger is nearer) follows the same a, b.
struct QuadMap
{
    QPointF s0, e1, e3;
    QPointF t0, d1, d3;
    double z0 = 0.0, dz1 = 0.0, dz3 = 0.0;
    double det = 0.0;
};

double depthOf(std::array<float, 3> corner)
{
    return static_cast<double>(corner[0]) + static_cast<double>(corner[1]) + static_cast<double>(corner[2]);
}

QuadMap quadMap(const Quad &quad, const QImage &atlas, int size)
{
    QuadMap map;
    map.s0 = project(quad.corners[0], size);
    map.e1 = project(quad.corners[1], size) - map.s0;
    map.e3 = project(quad.corners[3], size) - map.s0;
    map.t0 = QPointF(quad.uvs[0][0] * atlas.width(), quad.uvs[0][1] * atlas.height());
    map.d1 = QPointF(quad.uvs[1][0] * atlas.width(), quad.uvs[1][1] * atlas.height()) - map.t0;
    map.d3 = QPointF(quad.uvs[3][0] * atlas.width(), quad.uvs[3][1] * atlas.height()) - map.t0;
    map.z0 = depthOf(quad.corners[0]);
    map.dz1 = depthOf(quad.corners[1]) - map.z0;
    map.dz3 = depthOf(quad.corners[3]) - map.z0;
    map.det = map.e1.x() * map.e3.y() - map.e1.y() * map.e3.x();
    return map;
}

// The texels a quad maps to, inside the atlas.
struct TexelWindow
{
    int firstColumn = 0;
    int lastColumn = 0;
    int firstRow = 0;
    int lastRow = 0;
};

TexelWindow texelWindow(const QuadMap &map, const QImage &atlas)
{
    const QRectF texels = QRectF(map.t0, map.t0 + map.d1 + map.d3).normalized();
    TexelWindow window;
    window.firstColumn = std::clamp(static_cast<int>(std::floor(texels.left())), 0, atlas.width() - 1);
    window.lastColumn = std::clamp(static_cast<int>(std::ceil(texels.right())) - 1, window.firstColumn, atlas.width() - 1);
    window.firstRow = std::clamp(static_cast<int>(std::floor(texels.top())), 0, atlas.height() - 1);
    window.lastRow = std::clamp(static_cast<int>(std::ceil(texels.bottom())) - 1, window.firstRow, atlas.height() - 1);
    return window;
}

// The texel under `point` (atlas pixels), held inside the window.
Rgba texelAt(const QImage &atlas, TexelWindow window, QPointF point)
{
    const int column = std::clamp(static_cast<int>(std::floor(point.x())), window.firstColumn, window.lastColumn);
    const int row = std::clamp(static_cast<int>(std::floor(point.y())), window.firstRow, window.lastRow);
    return rgbaAt(atlas.constScanLine(row) + static_cast<qsizetype>(column) * 4);
}

// The icon being drawn and, per pixel, the depth of what it shows: a face behind another one that
// was drawn first stays hidden, so a pillow on a mattress shows although the mattress's top has
// the larger mean depth (the painter's order alone gets such boxes wrong).
class Canvas
{
public:
    explicit Canvas(int size)
        : m_image(size, size, QImage::Format_RGBA8888)
        , m_depth(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), -std::numeric_limits<double>::infinity())
    {
        m_image.fill(Qt::transparent);
    }

    void paint(const Quad &quad, const QImage &atlas);

    [[nodiscard]] QImage image() const { return m_image; }

private:
    QImage m_image;
    std::vector<double> m_depth;
};

// Draws one visible quad, its texture mapped from `atlas` (RGBA8888) without filtering: a pixel
// belongs to the quad when its centre lies inside and nothing nearer covers it, and takes the
// texel under that centre; transparent texels leave the pixel and its depth alone. Done here
// instead of with QPainter so that every platform draws the same pixels: Qt's transformed image
// drawing decides pixels whose centre lies within 1/64 pixel of an edge differently on x86-64
// and ARM (decision E-901).
void Canvas::paint(const Quad &quad, const QImage &atlas)
{
    // Faces meeting at an edge have the same depth there; the later one wins as in the painter's order.
    static constexpr double sameDepth = 1e-6;
    QImage &icon = m_image;
    const QuadMap map = quadMap(quad, atlas, icon.width());
    if (std::abs(map.det) < 1e-9) {
        return;
    }

    const QRectF bounds = QPolygonF{map.s0, map.s0 + map.e1, map.s0 + map.e1 + map.e3, map.s0 + map.e3}.boundingRect();
    const TexelWindow window = texelWindow(map, atlas);
    const int left = std::max(0, static_cast<int>(std::floor(bounds.left())));
    const int right = std::min(icon.width() - 1, static_cast<int>(std::ceil(bounds.right())));
    const int top = std::max(0, static_cast<int>(std::floor(bounds.top())));
    const int bottom = std::min(icon.height() - 1, static_cast<int>(std::ceil(bounds.bottom())));

    for (int y = top; y <= bottom; ++y) {
        uchar *line = icon.scanLine(y);
        for (int x = left; x <= right; ++x) {
            const double px = x + 0.5 - map.s0.x();
            const double py = y + 0.5 - map.s0.y();
            const double a = (px * map.e3.y() - py * map.e3.x()) / map.det;
            const double b = (map.e1.x() * py - map.e1.y() * px) / map.det;
            if (a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0) {
                continue;
            }
            const double depth = map.z0 + a * map.dz1 + b * map.dz3;
            double &shown = m_depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(icon.width()) + static_cast<std::size_t>(x)];
            if (depth < shown - sameDepth) {
                continue;
            }
            const Rgba texel = texelAt(atlas, window, map.t0 + a * map.d1 + b * map.d3);
            if (texel[3] != 0) {
                uchar *pixel = line + static_cast<qsizetype>(x) * 4;
                const Rgba blended = blendOver(rgbaAt(pixel), texel);
                std::ranges::copy(blended, pixel);
                shown = std::max(shown, depth);
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
    return render(QList<Part>{Part{&entry, {0, 0, 0}}}, shadedAtlas, size);
}

QImage render(const QList<Part> &parts, const std::array<QImage, 6> &shadedAtlas, int size)
{
    QList<Quad> quads = placedQuads(parts);

    // Painter's order, faces further from the viewer first: translucent faces blend over what lies
    // behind them, and of two faces at the same depth the nearer by mean wins.
    const auto depth = [](const Quad &quad) {
        float sum = 0.0f;
        for (const auto &p : quad.corners) {
            sum += p[0] + p[1] + p[2];
        }
        return sum;
    };
    std::ranges::stable_sort(quads, {}, depth);

    Canvas canvas(size);
    for (const Quad &quad : std::as_const(quads)) {
        if (visible(quad.face)) {
            canvas.paint(quad, shadedAtlas[static_cast<std::size_t>(quad.face)]);
        }
    }
    return canvas.image();
}

} // namespace QcBlockIcon
