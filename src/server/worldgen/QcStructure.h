#pragma once

#include "QcContentRegistry.h"
#include "QcResult.h"
#include "QcStructureDefinition.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// A structure whose palette is resolved to block states (decision E-219): read from a definition
// (structures/*.json) or assembled in code (the showcase's podium areas). Cells hold a palette index
// or keep what is there; the extras (creatures, contents, labels, updates, growing saplings) are
// in local coordinates like the cells.
class QcStructure
{
public:
    using Mob = QcStructureDefinition::Mob;
    using Stand = QcStructureDefinition::Stand;
    using Contents = QcStructureDefinition::Contents;
    using Label = QcStructureDefinition::Label;

    QcStructure(QString name, int sizeX, int sizeY, int sizeZ);

    // Unknown blocks fail; unknown state properties or values keep their defaults with a warning.
    static QcResult<QcStructure> fromDefinition(const QcStructureDefinition &definition, const QcContentRegistry &content,
                                                QStringList *warnings = nullptr);

    const QString &name() const { return m_name; }
    int sizeX() const { return m_sizeX; }
    int sizeY() const { return m_sizeY; }
    int sizeZ() const { return m_sizeZ; }
    bool contains(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < m_sizeX && y < m_sizeY && z < m_sizeZ; }

    // Precondition: contains(x, y, z).
    void set(int x, int y, int z, QcBlockId state);
    // The state of a cell, std::nullopt where the structure keeps what is there (or outside it).
    std::optional<QcBlockId> block(int x, int y, int z) const;
    // Palette index of a cell (or QcStructureDefinition::keep); precondition: contains(x, y, z).
    qint16 paletteIndex(int x, int y, int z) const { return m_cells[cellIndex(x, y, z)]; }
    const QList<QcBlockId> &palette() const { return m_palette; }

    QList<Mob> mobs;
    QList<Stand> stands;
    QList<Contents> contents;
    QList<Label> labels;
    QList<QcBlockPos> updates;
    QList<QcBlockPos> growing;

private:
    qsizetype cellIndex(int x, int y, int z) const { return x + qsizetype(m_sizeX) * (z + qsizetype(m_sizeZ) * y); }

    QString m_name;
    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    QList<QcBlockId> m_palette;
    QHash<QcBlockId, qint16> m_paletteIndex;
    QList<qint16> m_cells;
};
