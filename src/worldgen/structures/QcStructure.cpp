#include "worldgen/structures/QcStructure.h"

#include <limits>

using namespace Qt::StringLiterals;

QcStructure::QcStructure(QString name, int sizeX, int sizeY, int sizeZ)
    : m_name(std::move(name))
    , m_sizeX(std::max(0, sizeX))
    , m_sizeY(std::max(0, sizeY))
    , m_sizeZ(std::max(0, sizeZ))
    , m_cells(qsizetype(m_sizeX) * m_sizeY * m_sizeZ, QcStructureDefinition::keep)
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
            const int x = int(cell % definition.sizeX);
            const int z = int(cell / definition.sizeX % definition.sizeZ);
            const int y = int(cell / (qsizetype(definition.sizeX) * definition.sizeZ));
            structure.set(x, y, z, states[index]);
        }
    }

    structure.mobs = definition.mobs;
    structure.stands = definition.stands;
    structure.contents = definition.contents;
    structure.labels = definition.labels;
    structure.updates = definition.updates;
    structure.growing = definition.growing;
    structure.minecarts = definition.minecarts;
    structure.spawners = definition.spawners;
    structure.loot = definition.loot;
    structure.guardians = definition.guardians;
    structure.groundLayer = definition.groundLayer;
    return structure;
}

void QcStructure::set(int x, int y, int z, QcBlockId state)
{
    Q_ASSERT(contains(x, y, z));
    qint16 index = m_paletteIndex.value(state, QcStructureDefinition::keep);
    if (index < 0) {
        Q_ASSERT(m_palette.size() < std::numeric_limits<qint16>::max());
        index = qint16(m_palette.size());
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
