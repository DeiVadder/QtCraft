#pragma once

#include "QcAabb.h"
#include "QcChunkPos.h"
#include "QcDebugMessages.h"
#include "QcVec3.h"

#include <QHash>
#include <QList>
#include <QRgb>

class QcClientSession;

// One end of a debug line: position in the world and colour (RGBA bytes).
struct QcDebugLineVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    quint8 rgba[4] = {0, 0, 0, 0};
};
static_assert(sizeof(QcDebugLineVertex) == 16);

// The debug visualisations as coloured line segments in the world (decision E-225): chunk and
// section borders, hitboxes with the view direction, where creatures are heading, and sections
// that were meshed again. GUI-free; built on the GUI thread only while a visualisation is on, and
// drawn by the world renderer in one draw call.
class QcDebugLines
{
public:
    // Neighbouring chunks within this many chunks show their corners.
    static constexpr int chunkCornerRadius = 2;
    // Sections that were meshed again flash this long.
    static constexpr qint64 flashMs = 1000;

    static constexpr QRgb chunkCornerColor = qRgba(255, 70, 70, 255);
    static constexpr QRgb sectionColor = qRgba(255, 230, 60, 255);
    static constexpr QRgb chunkGridColor = qRgba(90, 200, 255, 200);
    static constexpr QRgb hitboxColor = qRgba(255, 255, 255, 255);
    static constexpr QRgb lookColor = qRgba(80, 140, 255, 255);
    static constexpr QRgb chaseColor = qRgba(255, 60, 60, 255);
    static constexpr QRgb wanderColor = qRgba(80, 230, 80, 255);
    static constexpr QRgb fleeColor = qRgba(255, 200, 40, 255);
    static constexpr QRgb flashColor = qRgba(255, 120, 255, 255);

    void addLine(const QcVec3 &from, const QcVec3 &to, QRgb color);
    void addBox(const QcAabb &box, QRgb color);

    // The player's chunk: its corners up to the top of the world, the section borders as rings
    // and a grid on its walls every four blocks; the corners of the chunks around it.
    void addChunkBorders(const QcVec3 &feet);
    // Boxes of the entities the session knows (positions interpolated by `alpha`) and their view
    // direction.
    void addHitboxes(const QcClientSession &session, double alpha);
    // From each creature to where it is heading, coloured by chasing, wandering and fleeing.
    void addMobGoals(const QcClientSession &session, const QList<QcMsg::MobGoal> &goals, double alpha);
    // Boxes of sections meshed within flashMs, fading; `meshedAt` maps section keys to the time of
    // their mesh, `now` in the same clock.
    void addRemeshFlashes(const QHash<quint64, qint64> &meshedAt, qint64 nowMs);

    // The box of a section (QcSectionKey).
    static QcAabb sectionBox(quint64 sectionKey);

    const QList<QcDebugLineVertex> &vertices() const { return m_vertices; }
    bool isEmpty() const { return m_vertices.isEmpty(); }

private:
    QList<QcDebugLineVertex> m_vertices;
};
