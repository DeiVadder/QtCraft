// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/structures/QcStructure.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/world/QcBlockAccess.h"

#include <limits>

using namespace Qt::StringLiterals;

namespace {

// The blocks of a structure as a world: air outside it and in the cells that keep what is there.
class QcStructureBlocks final : public QcBlockAccess
{
public:
    QcStructureBlocks(const QcStructure &structure, const QcContentRegistry &content)
        : m_structure(structure)
        , m_content(content)
    {
    }

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        return m_structure.block(pos.x, pos.y, pos.z).value_or(qcAirBlockId);
    }
    const QcContentRegistry &content() const override { return m_content; }

private:
    const QcStructure &m_structure;
    const QcContentRegistry &m_content;
};

// Whether a palette entry ("oak_fence[east=true,west=true]") sets a connection property itself.
bool namesConnection(const QString &entry)
{
    static const QStringList sides{u"north"_s, u"east"_s, u"south"_s, u"west"_s, u"up"_s};
    const qsizetype bracket = entry.indexOf(u'[');
    if (bracket < 0) {
        return false;
    }

    const QString properties = entry.mid(bracket + 1).chopped(entry.endsWith(u']') ? 1 : 0);
    const QStringList parts = properties.split(u',', Qt::SkipEmptyParts);
    for (const QString &property : parts) {
        if (sides.contains(property.section(u'=', 0, 0).trimmed())) {
            return true;
        }
    }
    return false;
}

// Whether `state` takes connection properties from its neighbours (rails and stairs keep theirs).
bool connects(const QcContentRegistry &content, QcBlockId state)
{
    if (!content.isValidBlock(state)) {
        return false;
    }
    const QcBlockDefinition &definition = content.block(state);
    return !definition.connectsTo.isEmpty() && !definition.rail && !definition.stairs;
}

// The state of the connecting block at `pos` with its connection properties from its neighbours in
// `blocks`; nothing when the block there does not connect.
std::optional<QcBlockId> connectedCell(const QcBlockAccess &blocks, QcBlockPos pos)
{
    const std::optional<QcBlockId> state = blocks.blockAt(pos);
    if (!state || !connects(blocks.content(), *state)) {
        return std::nullopt;
    }
    return QcBlockRules::connectedState(blocks, pos, *state);
}

} // namespace

QcStructure::QcStructure(QString name, int sizeX, int sizeY, int sizeZ)
    : m_name(std::move(name))
    , m_sizeX(std::max(0, sizeX))
    , m_sizeY(std::max(0, sizeY))
    , m_sizeZ(std::max(0, sizeZ))
    , m_cells(static_cast<qsizetype>(m_sizeX) * m_sizeY * m_sizeZ, QcStructureDefinition::keep)
{
}

QcStructure::Loaded QcStructure::fromDefinition(const QcStructureDefinition &definition, const QcContentRegistry &content)
{
    QList<QcBlockId> states;
    QStringList warnings;
    for (const QString &name : definition.palette) {
        const QcContentRegistry::ParsedState parsed = content.parseState(name);
        if (!parsed.id) {
            return std::unexpected(Failure{u"structure '%1': unknown block '%2'"_s.arg(definition.name, name), std::move(warnings)});
        }
        for (const QString &problem : parsed.problems) {
            warnings.append(u"structure '%1': %2"_s.arg(definition.name, problem));
        }
        states.append(*parsed.id);
    }

    QcStructure structure(definition.name, definition.sizeX, definition.sizeY, definition.sizeZ);
    structure.m_warnings = std::move(warnings);
    for (qsizetype cell = 0; cell < definition.cells.size(); ++cell) {
        const qint16 index = definition.cells[cell];
        if (index >= 0 && index < states.size()) {
            // Cells run x fastest, then z, then y (QcStructureDefinition::cellIndex).
            const int x = static_cast<int>(cell % definition.sizeX);
            const int z = static_cast<int>(cell / definition.sizeX % definition.sizeZ);
            const int y = static_cast<int>(cell / (static_cast<qsizetype>(definition.sizeX) * definition.sizeZ));
            structure.set(x, y, z, states[index]);
        }
    }
    structure.connectBlocks(content, looseCells(definition.palette, definition.cells));

    structure.mobs = definition.mobs;
    structure.stands = definition.stands;
    structure.contents = definition.contents;
    structure.labels = definition.labels;
    structure.updates = definition.updates;
    structure.growing = definition.growing;
    structure.adventure = definition.adventure;
    structure.minecarts = definition.minecarts;
    structure.boats = definition.boats;
    structure.spawners = definition.spawners;
    structure.loot = definition.loot;
    structure.signs = definition.signs;
    structure.hangings = definition.hangings;
    structure.guardians = definition.guardians;
    structure.spawnArea = definition.spawnArea;
    structure.groundLayer = definition.groundLayer;
    return structure;
}

void QcStructure::set(int x, int y, int z, QcBlockId state)
{
    Q_ASSERT(contains(x, y, z));
    qint16 index = m_paletteIndex.value(state, QcStructureDefinition::keep);
    if (index < 0) {
        Q_ASSERT(m_palette.size() < std::numeric_limits<qint16>::max());
        index = static_cast<qint16>(m_palette.size());
        m_paletteIndex.insert(state, index);
        m_palette.append(state);
    }
    m_cells[cellIndex(x, y, z)] = index;
}

std::optional<QcBlockId> QcStructure::block(int x, int y, int z) const
{
    if (!contains(x, y, z)) {
        return std::nullopt;
    }

    const qint16 index = m_cells[cellIndex(x, y, z)];
    if (index < 0) {
        return std::nullopt;
    }
    return m_palette[index];
}

QList<bool> QcStructure::looseCells(const QStringList &palette, const QList<qint16> &cells)
{
    QList<bool> looseEntries;
    for (const QString &entry : palette) {
        looseEntries.append(!namesConnection(entry));
    }

    QList<bool> loose;
    loose.reserve(cells.size());
    for (const qint16 index : cells) {
        loose.append(index >= 0 && index < looseEntries.size() && looseEntries[index]);
    }
    return loose;
}

void QcStructure::connectBlocks(const QcContentRegistry &content, const QList<bool> &loose)
{
    // The neighbours are read from the structure as it was, so the order of the cells does not matter.
    const QcStructure before = *this;
    const QcStructureBlocks blocks(before, content);
    const qsizetype cells = std::min(loose.size(), m_cells.size());
    for (qsizetype cell = 0; cell < cells; ++cell) {
        const QcBlockPos pos = cellPos(cell);
        const std::optional<QcBlockId> state = loose[cell] ? connectedCell(blocks, pos) : std::nullopt;
        if (state) {
            set(pos.x, pos.y, pos.z, *state);
        }
    }
}
