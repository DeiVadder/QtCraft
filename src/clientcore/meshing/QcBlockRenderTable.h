#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockVertex.h"
#include "QcLightTable.h"

#include <QList>
#include <QRectF>

#include <array>
#include <memory>

class QcContentRegistry;
class QcTextureAtlas;

// Per-state data the mesher needs, indexed by runtime block id (block state). Immutable after
// construction and shared with meshing threads.
class QcBlockRenderTable
{
public:
    // Rectangle on a cell side in the two coordinates that span it (see QcBlockRenderTable::axes):
    // a0..a1 along the first axis, b0..b1 along the second.
    struct SideRect
    {
        float a0 = 0.0f;
        float a1 = 1.0f;
        float b0 = 0.0f;
        float b1 = 1.0f;

        bool contains(SideRect other) const
        {
            return a0 <= other.a0 && a1 >= other.a1 && b0 <= other.b0 && b1 >= other.b1;
        }
    };

    // One face of a shape box, in block-local coordinates; corners are ordered like the full cube
    // faces (0 and 1 along the bottom edge of the texture).
    struct Quad
    {
        QcBlockFace face = QcBlockFace::Up;
        // The quad lies on the cell side `face` and may be hidden by the neighbour there.
        bool boundary = false;
        SideRect rect;
        std::array<std::array<float, 3>, 4> corners{};
        std::array<std::array<float, 2>, 4> uvs{};
    };

    struct Cross
    {
        std::array<float, 6> box{};
        QRectF uv;
    };

    struct Entry
    {
        QcRenderLayer layer = QcRenderLayer::Invisible;
        // Index of the block definition, to tell states of the same block.
        quint32 type = 0;
        bool opaque = false;
        bool liquid = false;
        // Height of a liquid's surface in its cell when no liquid lies on top (falls with the
        // flow level; sources and falling liquid are nearly full).
        float liquidHeight = 1.0f;
        // The state is the full cube: drawn from faceUv; otherwise from quads and crosses.
        bool fullCube = true;
        // Light lost when passing through this block (0..15) and block light emitted (0..15).
        int lightOpacity = 0;
        int lightLevel = 0;
        // Faces light cannot cross (QcLightPassage bits; covered faces of partial shapes).
        quint8 lightBlockedFaces = 0;
        // Surface class of the faces and quads in the vertices (crosses are QcSurfaceMaterial::Plant
        // unless the block is liquid, translucent or emissive).
        QcSurfaceMaterial material = QcSurfaceMaterial::Solid;

        // Whether light passes differently after a change from `other` to this entry.
        bool lightDiffers(const Entry &other) const
        {
            return lightOpacity != other.lightOpacity || lightBlockedFaces != other.lightBlockedFaces;
        }
        std::array<QRectF, 6> faceUv;
        QList<Quad> quads;
        QList<Cross> crosses;
        // Rectangles of the cell sides covered by box faces, per QcBlockFace; they hide the
        // matching faces of neighbours.
        std::array<QList<SideRect>, 6> cover;
    };

    // How far the surface of a full liquid lies below the top of its cell.
    static constexpr float liquidSurfaceDrop = 0.125f;

    QcBlockRenderTable() = default;
    QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas);

    // Unknown ids render as invisible.
    const Entry &entry(QcBlockId id) const;
    qsizetype size() const { return m_entries.size(); }
    bool hasLightSources() const { return m_hasLightSources; }
    // How the states treat light, as the light engine reads it (the same values as the entries).
    const std::shared_ptr<const QcLightTable> &lightTable() const { return m_lightTable; }

    // The axes (0 = x, 1 = y, 2 = z) along which side rectangles of a face are measured.
    static std::pair<int, int> axes(QcBlockFace face);
    // Whether a face of `self` with rectangle `rect` on the side towards `neighbour` is hidden by it.
    static bool hides(const Entry &self, SideRect rect, QcBlockFace face, const Entry &neighbour);

private:
    QList<Entry> m_entries;
    Entry m_invisible;
    bool m_hasLightSources = false;
    std::shared_ptr<const QcLightTable> m_lightTable = std::make_shared<const QcLightTable>();
};
