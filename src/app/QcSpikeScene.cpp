#include "QcSpikeScene.h"

#include "QcTextureSynthesizer.h"
#include "QcWorldView.h"

#include <QPainter>

#include <array>

namespace {

constexpr int tilesPerRow = 4;

enum Tile { GrassTop, GrassSide, Dirt, Stone, LogSide, LogTop, Leaves, Water, TileCount };

QImage buildAtlas()
{
    using Pattern = QcTextureSpec::Pattern;
    const std::array<QcTextureSpec, TileCount> specs = {{
        {Pattern::Speckle, QColor(92, 160, 64), QColor(70, 132, 48), 0.12, 1},
        {Pattern::GrassSide, QColor(134, 96, 67), QColor(92, 160, 64), 0.12, 2},
        {Pattern::Speckle, QColor(134, 96, 67), QColor(110, 78, 52), 0.12, 3},
        {Pattern::Speckle, QColor(125, 125, 125), QColor(105, 105, 105), 0.1, 4},
        {Pattern::LogSide, QColor(110, 84, 50), QColor(80, 60, 36), 0.1, 5},
        {Pattern::LogTop, QColor(170, 135, 85), QColor(110, 84, 50), 0.08, 6},
        {Pattern::Leaves, QColor(60, 130, 50), QColor(45, 105, 40), 0.15, 7},
        {Pattern::Water, QColor(50, 90, 200, 170), QColor(50, 90, 200, 170), 0.08, 8},
    }};
    constexpr int size = QcTextureSynthesizer::tileSize;
    QImage atlas(size * tilesPerRow, size * tilesPerRow, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);
    QPainter painter(&atlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int tile = 0; tile < TileCount; ++tile) {
        painter.drawImage((tile % tilesPerRow) * size, (tile / tilesPerRow) * size, QcTextureSynthesizer::synthesize(specs[tile]));
    }
    return atlas;
}

void addFace(QcChunkMesh &mesh, bool translucent, const std::array<QVector3D, 4> &corners, int tile, float shade)
{
    QList<QcBlockVertex> &vertices = translucent ? mesh.translucentVertices : mesh.opaqueVertices;
    QList<quint32> &indices = translucent ? mesh.translucentIndices : mesh.opaqueIndices;
    const float tileSpan = 1.0f / tilesPerRow;
    const float u0 = float(tile % tilesPerRow) * tileSpan;
    const float v0 = float(tile / tilesPerRow) * tileSpan; // NOLINT(bugprone-integer-division): row index
    const std::array<std::pair<float, float>, 4> uvs = {{{u0, v0 + tileSpan}, {u0 + tileSpan, v0 + tileSpan},
                                                         {u0 + tileSpan, v0}, {u0, v0}}};
    const auto base = quint32(vertices.size());
    for (int i = 0; i < 4; ++i) {
        vertices.append({corners[i].x(), corners[i].y(), corners[i].z(), uvs[i].first, uvs[i].second, shade, 1.0f});
    }
    indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
}

void addCube(QcChunkMesh &mesh, int x, int y, int z, int top, int side, int bottom, bool translucent = false)
{
    const float x0 = float(x);
    const float y0 = float(y);
    const float z0 = float(z);
    const float x1 = x0 + 1.0f;
    const float y1 = y0 + 1.0f;
    const float z1 = z0 + 1.0f;
    // Counter-clockwise when seen from outside; corners start bottom-left of the texture.
    addFace(mesh, translucent, {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, top, 1.0f);
    addFace(mesh, translucent, {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, bottom, 0.5f);
    addFace(mesh, translucent, {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, side, 0.8f);
    addFace(mesh, translucent, {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}}, side, 0.8f);
    addFace(mesh, translucent, {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}}, side, 0.65f);
    addFace(mesh, translucent, {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, side, 0.65f);
}

} // namespace

void QcSpikeScene::populate(QcWorldView *view)
{
    view->setAtlas(buildAtlas());
    auto mesh = std::make_shared<QcChunkMesh>();
    for (int x = -8; x < 8; ++x) {
        for (int z = -8; z < 8; ++z) {
            addCube(*mesh, x, 0, z, Stone, Stone, Stone);
            const bool pond = x >= 2 && x <= 5 && z >= -6 && z <= -3;
            if (pond) {
                addCube(*mesh, x, 1, z, Water, Water, Water, true);
            } else {
                addCube(*mesh, x, 1, z, Dirt, Dirt, Dirt);
                addCube(*mesh, x, 2, z, GrassTop, GrassSide, Dirt);
            }
        }
    }
    for (int y = 3; y < 7; ++y) {
        addCube(*mesh, -3, y, -3, LogTop, LogSide, LogTop);
    }
    for (int x = -5; x <= -1; ++x) {
        for (int z = -5; z <= -1; ++z) {
            for (int y = 6; y <= 7; ++y) {
                if (x != -3 || z != -3 || y == 7) {
                    addCube(*mesh, x, y, z, Leaves, Leaves, Leaves);
                }
            }
        }
    }
    view->submitMesh(1, mesh);

    QcCamera camera;
    camera.position = QVector3D(9.0f, 9.0f, 11.0f);
    camera.yaw = -40.0f;
    camera.pitch = -28.0f;
    view->setCamera(camera);
}
