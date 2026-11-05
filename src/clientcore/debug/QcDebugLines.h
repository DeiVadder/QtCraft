// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcChunkPos.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcDebugMessages.h"

#include <QHash>
#include <QList>
#include <QRgb>

class QcClientSession;

/*! \brief One end of a debug line: position in the world and colour (RGBA bytes). */
struct QcDebugLineVertex
{
    /*! East, relative to the lines' origin. */
    float x = 0.0f;
    /*! Up, relative to the lines' origin. */
    float y = 0.0f;
    /*! South, relative to the lines' origin. */
    float z = 0.0f;
    /*! Red, green, blue and alpha. */
    quint8 rgba[4] = {0, 0, 0, 0};
};
static_assert(sizeof(QcDebugLineVertex) == 16);

/*! \brief The debug visualisations as coloured line segments in the world (decision E-225): chunk
    and section borders, hitboxes with the view direction, where creatures are heading, and sections
    that were meshed again.

    GUI-free; built on the GUI thread only while a visualisation is on, and drawn by the world
    renderer in one draw call. */
class QcDebugLines
{
public:
    /*! Neighbouring chunks within this many chunks show their corners. */
    static constexpr int chunkCornerRadius = 2;
    /*! Sections that were meshed again flash this long. */
    static constexpr qint64 flashMs = 1000;

    /*! The corners of the chunks around the player's. */
    static constexpr QRgb chunkCornerColor = qRgba(255, 70, 70, 255);
    /*! The player's chunk: its corners and the section borders. */
    static constexpr QRgb sectionColor = qRgba(255, 230, 60, 255);
    /*! The grid on the walls of the player's chunk. */
    static constexpr QRgb chunkGridColor = qRgba(90, 200, 255, 200);
    /*! Entity boxes. */
    static constexpr QRgb hitboxColor = qRgba(255, 255, 255, 255);
    /*! An entity's view direction. */
    static constexpr QRgb lookColor = qRgba(80, 140, 255, 255);
    /*! A creature chasing. */
    static constexpr QRgb chaseColor = qRgba(255, 60, 60, 255);
    /*! A creature wandering. */
    static constexpr QRgb wanderColor = qRgba(80, 230, 80, 255);
    /*! A creature fleeing. */
    static constexpr QRgb fleeColor = qRgba(255, 200, 40, 255);
    /*! A section meshed again. */
    static constexpr QRgb flashColor = qRgba(255, 120, 255, 255);

    /*! The vertices are placed relative to \a origin (decision E-883), near where the lines are
        drawn. */
    explicit QcDebugLines(const QcVec3 &origin)
        : m_origin(origin)
    {
    }

    /*! Adds a line from \a from to \a to in \a color. */
    void addLine(const QcVec3 &from, const QcVec3 &to, QRgb color);
    /*! Adds the twelve edges of \a box in \a color. */
    void addBox(const QcAabb &box, QRgb color);

    /*! The chunk of the player's \a feet: its corners up to the top of the world, the section
        borders as rings and a grid on its walls every four blocks; the corners of the chunks
        around it. */
    void addChunkBorders(const QcVec3 &feet);
    /*! Boxes of the entities \a session knows (positions interpolated by \a alpha) and their
        view direction. */
    void addHitboxes(const QcClientSession &session, double alpha);
    /*! From each creature of \a session (interpolated by \a alpha) to where it is heading
        (\a goals), coloured by chasing, wandering and fleeing. */
    void addMobGoals(const QcClientSession &session, const QList<QcMsg::MobGoal> &goals, double alpha);
    /*! Boxes of sections meshed within flashMs, fading; \a meshedAt maps section keys to the
        time of their mesh, \a nowMs in the same clock. */
    void addRemeshFlashes(const QHash<quint64, qint64> &meshedAt, qint64 nowMs);

    /*! Returns the box of the section \a sectionKey (qcSectionKey()). */
    static QcAabb sectionBox(quint64 sectionKey);

    /*! Returns the vertices, relative to origin(). */
    const QList<QcDebugLineVertex> &vertices() const { return m_vertices; }
    /*! Returns the point the vertices are relative to. */
    const QcVec3 &origin() const { return m_origin; }
    /*! Returns whether there is no line. */
    bool isEmpty() const { return m_vertices.isEmpty(); }

private:
    QcVec3 m_origin;
    QList<QcDebugLineVertex> m_vertices;
};
