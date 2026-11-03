// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcBlockVertex.h"
#include "common/content/blocks/QcBlockDefinition.h"
#include "common/light/QcLightTable.h"

#include <QList>
#include <QRectF>

#include <array>
#include <memory>

class QcContentRegistry;
class QcTextureAtlas;

/*! \brief Per-state data the mesher needs, indexed by runtime block id (block state).

    Immutable after construction and shared with meshing threads. */
class QcBlockRenderTable
{
public:
    /*! \brief Rectangle on a cell side in the two coordinates that span it (see
        QcBlockRenderTable::axes): a0..a1 along the first axis, b0..b1 along the second. */
    struct SideRect
    {
        /*! Start along the first axis (0..1). */
        float a0 = 0.0f;
        /*! End along the first axis (0..1). */
        float a1 = 1.0f;
        /*! Start along the second axis (0..1). */
        float b0 = 0.0f;
        /*! End along the second axis (0..1). */
        float b1 = 1.0f;

        /*! Returns whether this rectangle covers all of \a other. */
        bool contains(SideRect other) const
        {
            return a0 <= other.a0 && a1 >= other.a1 && b0 <= other.b0 && b1 >= other.b1;
        }

        bool operator==(const SideRect &) const = default;
    };

    /*! \brief One face of a shape box, in block-local coordinates; corners are ordered like the
        full cube faces (0 and 1 along the bottom edge of the texture). */
    struct Quad
    {
        /*! Direction the quad faces. */
        QcBlockFace face = QcBlockFace::Up;
        /*! The quad lies on the cell side \c {face} and may be hidden by the neighbour there. */
        bool boundary = false;
        /*! Rectangle the quad takes on its cell side (only meaningful when \c {boundary}). */
        SideRect rect;
        /*! Corners in block-local coordinates (0..1). */
        std::array<std::array<float, 3>, 4> corners{};
        /*! Atlas coordinates of the corners. */
        std::array<std::array<float, 2>, 4> uvs{};

        bool operator==(const Quad &) const = default;
    };

    /*! \brief A pair of crossed planes (plants, saplings) spanning a box of the cell. */
    struct Cross
    {
        /*! The box as min x, y, z and max x, y, z in block-local coordinates. */
        std::array<float, 6> box{};
        /*! Atlas rectangle of the planes' texture. */
        QRectF uv;

        bool operator==(const Cross &) const = default;
    };

    /*! \brief What the mesher needs of one block state. */
    struct Entry
    {
        /*! Render layer the state is drawn in (Invisible: not drawn). */
        QcRenderLayer layer = QcRenderLayer::Invisible;
        /*! Index of the block definition, to tell states of the same block. */
        quint32 type = 0;
        /*! The state is an opaque full cube that hides its neighbours' faces. */
        bool opaque = false;
        /*! The state is a liquid. */
        bool liquid = false;
        /*! Height of a liquid's surface in its cell when no liquid lies on top (falls with the
            flow level; sources and falling liquid are nearly full). */
        float liquidHeight = 1.0f;
        /*! A liquid beside it slopes down into its cell (QcLiquidSurface::open, decision E-885):
            air and blocks without collision (torches, plants, rails, signs) or lower than half a
            block (thin snow layers, carpets). Solid blocks (also invisible ones), liquids and
            unknown ids are walls. */
        bool liquidMayEnter = false;
        /*! The state is the full cube: drawn from faceUv; otherwise from quads and crosses. */
        bool fullCube = true;
        /*! Light lost when passing through this block (0..15) and block light emitted (0..15).
            \qcshared */
        int lightOpacity = 0;
        int lightLevel = 0;
        /*! Faces light cannot cross (QcLightPassage bits; covered faces of partial shapes). */
        quint8 lightBlockedFaces = 0;
        /*! Surface class of the faces and quads in the vertices (crosses are
            QcSurfaceMaterial::Plant unless the block is liquid, translucent or emissive). */
        QcSurfaceMaterial material = QcSurfaceMaterial::Solid;

        /*! Returns whether light passes differently after a change from \a other to this entry. */
        bool lightDiffers(const Entry &other) const
        {
            return lightOpacity != other.lightOpacity || lightBlockedFaces != other.lightBlockedFaces;
        }
        /*! Atlas rectangles of the full cube's faces, per QcBlockFace. */
        std::array<QRectF, 6> faceUv;
        /*! Quarter turns of the full cube's face textures (QcBlockState::faceUvTurns); the quads
            of a shape carry theirs in their texture coordinates. */
        std::array<quint8, 6> faceUvTurns{};
        /*! Faces of the state's shape boxes when it is not the full cube. */
        QList<Quad> quads;
        /*! Crossed planes of the state. */
        QList<Cross> crosses;
        /*! Rectangles of the cell sides covered by box faces, per QcBlockFace; they hide the
            matching faces of neighbours. */
        std::array<QList<SideRect>, 6> cover;

        /*! Returns whether the entries are equal. Equal entries draw and pass light alike: the
            mesher reads nothing else of a state, so a change between two such states (a target's
            power, a note block's note) changes no mesh. */
        bool operator==(const Entry &) const = default;
    };

    /*! How far the surface of a full liquid lies below the top of its cell. */
    static constexpr float liquidSurfaceDrop = 0.125f;

    /*! Returns the height of the surface of the liquid \a id of \a content in its cell when no
        liquid lies on top (Entry::liquidHeight); 1 for anything that is not a liquid. */
    [[nodiscard]] static float liquidHeight(const QcContentRegistry &content, QcBlockId id);

    QcBlockRenderTable() = default;
    QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas);

    /*! Returns the entry of state \a id; unknown ids render as invisible. */
    const Entry &entry(QcBlockId id) const;
    /*! Returns the number of states in the table. */
    qsizetype size() const { return m_entries.size(); }
    /*! Returns whether any state emits block light. */
    bool hasLightSources() const { return m_hasLightSources; }
    /*! Returns how the states treat light, as the light engine reads it (the same values as the
        entries). */
    const std::shared_ptr<const QcLightTable> &lightTable() const { return m_lightTable; }

    /*! Returns the quads \a entry is drawn with on its own (an icon, a falling or sliding
        block): a shape's quads, a full cube's six faces spelled out (their texture turns left
        aside). */
    static QList<Quad> drawnQuads(const Entry &entry);
    /*! Returns the axes (0 = x, 1 = y, 2 = z) along which side rectangles of \a face are
        measured. */
    static std::pair<int, int> axes(QcBlockFace face);
    /*! Returns whether the face \a face of \a self with rectangle \a rect on the side towards
        \a neighbour is hidden by it. */
    static bool hides(const Entry &self, SideRect rect, QcBlockFace face, const Entry &neighbour);
    /*! Returns whether \a neighbour, a translucent liquid (water), stands for the boundary
        instead of the face \a face (Up or Down) of \a self, a translucent non-liquid block (ice,
        glass) next to it: always above the block, below it only over source or falling water
        (full surface). Translucent faces are drawn unsorted without depth writes, so two faces on
        one boundary blend in mesh order and the result flips from block to block (E-868). */
    static bool liquidStandsFor(const Entry &self, QcBlockFace face, const Entry &neighbour);
    /*! Returns whether \a self, a translucent non-liquid block (ice, glass), meets \a neighbour,
        a translucent liquid, at a shore: there the liquid keeps its side up to its surface and the
        block's side starts at that surface, so the liquid stays visible through the block and no
        two unsorted translucent faces lie on one boundary (decision E-959). */
    static bool isShore(const Entry &self, const Entry &neighbour);

private:
    QList<Entry> m_entries;
    Entry m_invisible;
    bool m_hasLightSources = false;
    std::shared_ptr<const QcLightTable> m_lightTable = std::make_shared<const QcLightTable>();
};
