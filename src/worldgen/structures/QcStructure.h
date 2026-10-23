#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
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

    // Why a definition could not be resolved, with the warnings of the palette entries before it.
    struct Failure
    {
        QString error;
        QStringList warnings;
    };

    using Loaded = std::expected<QcStructure, Failure>;

    // Unknown blocks fail; unknown state properties or values keep their defaults with a warning
    // (warnings(), or Failure::warnings for the entries before an unknown block).
    [[nodiscard]] static Loaded fromDefinition(const QcStructureDefinition &definition, const QcContentRegistry &content);
    // The warnings of a resolution, whether it succeeded or not (callers report them before the outcome).
    [[nodiscard]] static const QStringList &warningsOf(const Loaded &loaded) { return loaded ? loaded->warnings() : loaded.error().warnings; }

    const QString &name() const { return m_name; }
    int sizeX() const { return m_sizeX; }
    int sizeY() const { return m_sizeY; }
    int sizeZ() const { return m_sizeZ; }
    bool contains(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < m_sizeX && y < m_sizeY && z < m_sizeZ; }

    // Precondition: contains(x, y, z).
    void set(int x, int y, int z, QcBlockId state);
    // The state of a cell, std::nullopt where the structure keeps what is there (or outside it).
    [[nodiscard]] std::optional<QcBlockId> block(int x, int y, int z) const;
    // Palette index of a cell (or QcStructureDefinition::keep); precondition: contains(x, y, z).
    qint16 paletteIndex(int x, int y, int z) const { return m_cells[cellIndex(x, y, z)]; }
    const QList<QcBlockId> &palette() const { return m_palette; }
    // What fromDefinition() kept at a default (unknown state properties or values).
    const QStringList &warnings() const { return m_warnings; }

    // Per cell of `cells` (indices into `palette`, cell order of QcStructureDefinition::cellIndex):
    // whether the cell takes its connection properties from its neighbours, that is its palette
    // entry names none of north, east, south, west and up (decision E-867).
    [[nodiscard]] static QList<bool> looseCells(const QStringList &palette, const QList<qint16> &cells);
    // Sets the connection properties of the connecting blocks (fences, walls, panes, bars) in the
    // cells marked in `loose` from their neighbours inside the structure, with the rules of
    // QcBlockRules::connectedState (decision E-867). Cells outside the structure and cells that keep
    // what is there count as air: a fence at the edge does not reach out into the terrain.
    void connectBlocks(const QcContentRegistry &content, const QList<bool> &loose);

    // The extras of a structure assembled in code (the showcase's areas).
    void addMob(Mob mob) { mobs.append(std::move(mob)); }
    void addContents(Contents placed) { contents.append(std::move(placed)); }
    void addLabel(Label label) { labels.append(std::move(label)); }
    void addGrowing(QcBlockPos cell) { growing.append(cell); }
    void addCart(QcStructureDefinition::Cart cart) { minecarts.append(std::move(cart)); }

    QList<Mob> mobs;
    QList<Stand> stands;
    QList<Contents> contents;
    QList<Label> labels;
    QList<QcBlockPos> updates;
    QList<QcBlockPos> growing;
    QList<QcStructureDefinition::Cart> minecarts;
    QList<QcStructureDefinition::Spawner> spawners;
    QList<QcStructureDefinition::Loot> loot;
    QList<QcStructureDefinition::Sign> signs;
    QList<QcStructureDefinition::Hanging> hangings;
    // Zones whose guardians come back (decision E-591) and the layer at the ground's top block
    // (decision E-590).
    QList<QcStructureDefinition::Guardian> guardians;
    int groundLayer = 0;
    // Allow-list entries of adventure mode the showcase adds to the world (decision E-735).
    QcAdventureRules adventure;

private:
    qsizetype cellIndex(int x, int y, int z) const { return x + static_cast<qsizetype>(m_sizeX) * (z + static_cast<qsizetype>(m_sizeZ) * y); }
    // The position of a cell index: x runs fastest, then z, then y.
    QcBlockPos cellPos(qsizetype cell) const
    {
        const qsizetype layer = static_cast<qsizetype>(m_sizeX) * m_sizeZ;
        return QcBlockPos{static_cast<int>(cell % m_sizeX), static_cast<int>(cell / layer), static_cast<int>(cell / m_sizeX % m_sizeZ)};
    }

    QString m_name;
    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    QList<QcBlockId> m_palette;
    QHash<QcBlockId, qint16> m_paletteIndex;
    QList<qint16> m_cells;
    QStringList m_warnings;
};
