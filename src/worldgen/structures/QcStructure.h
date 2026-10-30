// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/core/QcResult.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

/*! \brief A structure whose palette is resolved to block states (decision E-219): read from a
    definition (structures/\unicode {0x2A}.json) or assembled in code (the showcase's podium areas).

    Cells hold a palette index or keep what is there; the extras (creatures, contents, labels,
    updates, growing saplings) are in local coordinates like the cells. */
class QcStructure
{
public:
    /*! A creature of the structure (QcStructureDefinition::Mob). */
    using Mob = QcStructureDefinition::Mob;
    /*! An armour stand of the structure (QcStructureDefinition::Stand). */
    using Stand = QcStructureDefinition::Stand;
    /*! The contents of a chest or machine of the structure (QcStructureDefinition::Contents). */
    using Contents = QcStructureDefinition::Contents;
    /*! A floating label of the structure (QcStructureDefinition::Label). */
    using Label = QcStructureDefinition::Label;

    QcStructure(QString name, int sizeX, int sizeY, int sizeZ);

    /*! \brief Why a definition could not be resolved, with the warnings of the palette entries
        before it. */
    struct Failure
    {
        /*! The message naming the structure and the unknown block. */
        QString error;
        /*! The warnings of the palette entries resolved before the unknown block. */
        QStringList warnings;
    };

    /*! The result of fromDefinition(): the structure or why it failed. */
    using Loaded = std::expected<QcStructure, Failure>;

    /*! Returns the structure of \a definition with its palette resolved to block states of
        \a content, with the connection properties of its connecting blocks set (connectBlocks())
        and its extras copied. Unknown blocks fail; unknown state properties or values keep their
        defaults with a warning (warnings(), or Failure::warnings for the entries before an
        unknown block). */
    [[nodiscard]] static Loaded fromDefinition(const QcStructureDefinition &definition, const QcContentRegistry &content);
    /*! Returns the warnings of the resolution \a loaded, whether it succeeded or not (callers
        report them before the outcome). */
    [[nodiscard]] static const QStringList &warningsOf(const Loaded &loaded) { return loaded ? loaded->warnings() : loaded.error().warnings; }

    /*! Returns the structure's name. */
    const QString &name() const { return m_name; }
    /*! Returns the size along x in blocks (negative sizes given to the constructor become 0). */
    int sizeX() const { return m_sizeX; }
    /*! Returns the size along y in blocks. */
    int sizeY() const { return m_sizeY; }
    /*! Returns the size along z in blocks. */
    int sizeZ() const { return m_sizeZ; }
    /*! Returns whether the local cell \a x, \a y, \a z lies inside the structure. */
    bool contains(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < m_sizeX && y < m_sizeY && z < m_sizeZ; }

    /*! Sets the cell \a x, \a y, \a z to \a state, adding the state to the palette when it is
        new. Precondition: contains(x, y, z). */
    void set(int x, int y, int z, QcBlockId state);
    /*! Returns the state of the cell \a x, \a y, \a z, std::nullopt where the structure keeps
        what is there (or outside it). */
    [[nodiscard]] std::optional<QcBlockId> block(int x, int y, int z) const;
    /*! Returns the palette index of the cell \a x, \a y, \a z (or QcStructureDefinition::keep);
        precondition: contains(x, y, z). */
    qint16 paletteIndex(int x, int y, int z) const { return m_cells[cellIndex(x, y, z)]; }
    /*! Returns the block states the cells refer to, in the order they were first set. */
    const QList<QcBlockId> &palette() const { return m_palette; }
    /*! What fromDefinition() kept at a default (unknown state properties or values). */
    const QStringList &warnings() const { return m_warnings; }

    /*! Per cell of \a cells (indices into \a palette, cell order of
        QcStructureDefinition::cellIndex): whether the cell takes its connection properties from
        its neighbours, that is its palette entry names none of north, east, south, west and up
        (decision E-867). */
    [[nodiscard]] static QList<bool> looseCells(const QStringList &palette, const QList<qint16> &cells);
    /*! Sets the connection properties of the connecting blocks (fences, walls, panes, bars) in
        the cells marked in \a loose from their neighbours inside the structure, with the rules
        of QcBlockRules::connectedState and the block definitions of \a content (decision E-867).
        Rails and stairs keep theirs. Cells outside the structure and cells
        that keep what is there count as air: a fence at the edge does not reach out into the
        terrain. */
    void connectBlocks(const QcContentRegistry &content, const QList<bool> &loose);

    /*! Adds the creature \a mob to a structure assembled in code (the showcase's areas). */
    void addMob(Mob mob) { mobs.append(std::move(mob)); }
    /*! Adds the chest or machine contents \a placed to a structure assembled in code. */
    void addContents(Contents placed) { contents.append(std::move(placed)); }
    /*! Adds the floating label \a label to a structure assembled in code. */
    void addLabel(Label label) { labels.append(std::move(label)); }
    /*! Adds the sapling at the local \a cell, which starts growing when the world is made. */
    void addGrowing(QcBlockPos cell) { growing.append(cell); }
    /*! Adds the minecart \a cart to a structure assembled in code. */
    void addCart(QcStructureDefinition::Cart cart) { minecarts.append(std::move(cart)); }

    /*! The creatures, in local coordinates. */
    QList<Mob> mobs;
    /*! The armour stands, in local coordinates. */
    QList<Stand> stands;
    /*! The contents of chests and machines, at local cells. */
    QList<Contents> contents;
    /*! The floating labels, in local coordinates. */
    QList<Label> labels;
    /*! Local cells whose block updates are queued (and circuits woken) when the world is made. */
    QList<QcBlockPos> updates;
    /*! Local cells of saplings that start growing when the world is made. */
    QList<QcBlockPos> growing;
    /*! Minecarts standing on rails when the world is made (decision E-382). */
    QList<QcStructureDefinition::Cart> minecarts;
    /*! Boats lying on water or ground when the world is made (decision E-920). */
    QList<QcStructureDefinition::Boat> boats;
    /*! Monster spawners (decision E-520). */
    QList<QcStructureDefinition::Spawner> spawners;
    /*! Chests filled from a loot table when their chunk first arrives (decision E-521). */
    QList<QcStructureDefinition::Loot> loot;
    /*! Sign texts written when the world is made (decision E-766). */
    QList<QcStructureDefinition::Sign> signs;
    /*! Item frames and paintings hung when the world is made (decision E-797). */
    QList<QcStructureDefinition::Hanging> hangings;
    /*! Zones whose guardians come back (decision E-591). */
    QList<QcStructureDefinition::Guardian> guardians;
    /*! The monsters of its footprint (decision E-871). */
    std::optional<QcStructureDefinition::SpawnArea> spawnArea;
    /*! The layer at the ground's top block where the world generator places the structure
        (decision E-590); 0 for structures that stand on the ground. */
    int groundLayer = 0;
    /*! Returns whether placed copies have guardian zones or a spawn area (QcStructureSites). */
    bool hasSites() const { return !guardians.isEmpty() || spawnArea.has_value(); }
    /*! Allow-list entries of adventure mode the showcase adds to the world (decision E-735). */
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
