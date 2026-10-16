#include "QcPieceVariation.h"

using namespace Qt::StringLiterals;

namespace {

// "qtcraft:oak_stairs[facing=north]" with its block name swapped, states kept.
QString swappedState(const QString &state, const QHash<QString, QString> &swaps)
{
    const qsizetype bracket = state.indexOf(u'[');
    const QString block = bracket < 0 ? state : state.left(bracket);
    const auto it = swaps.constFind(block);
    if (it == swaps.cend()) {
        return state;
    }
    return *it + (bracket < 0 ? QString() : state.mid(bracket));
}

} // namespace

QcResult<QcPieceVariation> QcPieceVariation::resolve(const QcStructureDefinition &definition, const QcContentRegistry &content,
                                                     QStringList *warnings)
{
    QcResult<QcStructure> base = QcStructure::fromDefinition(definition, content, warnings);
    if (!base) {
        return qcError(base.error());
    }

    QcPieceVariation variation;
    variation.m_base = std::make_shared<const QcStructure>(std::move(*base));
    if (definition.choices.isEmpty() && definition.parts.isEmpty()) {
        return variation;
    }

    // Every palette entry is known here: fromDefinition() resolved them all.
    for (const QString &state : definition.palette) {
        variation.m_states.append(content.stateId(state).value_or(qcAirBlockId));
    }
    variation.m_cells = definition.cells;
    for (const QcStructureDefinition::Choice &choice : definition.choices) {
        QList<Option> options;
        for (const QHash<QString, QString> &swaps : choice.options) {
            options.append(resolveOption(definition, swaps, content, warnings));
        }
        variation.m_choices.append(std::move(options));
    }
    variation.m_parts = definition.parts;
    return variation;
}

QcPieceVariation::Option QcPieceVariation::resolveOption(const QcStructureDefinition &definition, const QHash<QString, QString> &swaps,
                                                         const QcContentRegistry &content, QStringList *warnings)
{
    Option option;
    option.mobs = swaps;
    for (qsizetype i = 0; i < definition.palette.size(); ++i) {
        const QString state = swappedState(definition.palette[i], swaps);
        if (state == definition.palette[i]) {
            continue;
        }
        if (const std::optional<QcBlockId> id = content.stateId(state)) {
            option.states.append({i, *id});
        } else if (warnings) {
            warnings->append(u"structure '%1': unknown block '%2' in a choice"_s.arg(definition.name, state));
        }
    }
    return option;
}

QList<int> QcPieceVariation::optionCounts() const
{
    QList<int> counts;
    for (const QList<Option> &options : m_choices) {
        counts.append(int(options.size()));
    }
    return counts;
}

QList<double> QcPieceVariation::partChances() const
{
    QList<double> chances;
    for (const QcStructureDefinition::Part &part : m_parts) {
        chances.append(part.chance);
    }
    return chances;
}

QcPieceVariation::Selection QcPieceVariation::choose(const std::function<double()> &random) const
{
    Selection selection;
    for (const QList<Option> &options : m_choices) {
        selection.options.append(std::min(int(options.size()) - 1, int(random() * double(options.size()))));
    }
    for (const QcStructureDefinition::Part &part : m_parts) {
        selection.parts.append(random() < part.chance);
    }
    return selection;
}

QcPieceVariation::Selection QcPieceVariation::everything() const
{
    Selection selection;
    selection.options.fill(0, m_choices.size());
    selection.parts.fill(true, m_parts.size());
    return selection;
}

std::shared_ptr<const QcStructure> QcPieceVariation::build(const Selection &selection) const
{
    if (!varies()) {
        return m_base;
    }

    QList<QcBlockId> states = m_states;
    QHash<QString, QString> mobSwaps;
    for (qsizetype c = 0; c < m_choices.size() && c < selection.options.size(); ++c) {
        const Option &option = m_choices[c][selection.options[c]];
        for (const auto &[index, state] : option.states) {
            states[index] = state;
        }
        mobSwaps.insert(option.mobs);
    }
    QList<qint16> cells = m_cells;
    for (qsizetype p = 0; p < m_parts.size() && p < selection.parts.size(); ++p) {
        if (!selection.parts[p]) {
            continue;
        }
        for (qsizetype cell = 0; cell < cells.size(); ++cell) {
            if (m_parts[p].cells[cell] != QcStructureDefinition::keep) {
                cells[cell] = m_parts[p].cells[cell];
            }
        }
    }

    const QcStructure &base = *m_base;
    auto structure = std::make_shared<QcStructure>(base.name(), base.sizeX(), base.sizeY(), base.sizeZ());
    for (qsizetype cell = 0; cell < cells.size(); ++cell) {
        if (cells[cell] >= 0) {
            // Cells run x fastest, then z, then y (QcStructureDefinition::cellIndex).
            const int x = int(cell % base.sizeX());
            const int z = int(cell / base.sizeX() % base.sizeZ());
            const int y = int(cell / (qsizetype(base.sizeX()) * base.sizeZ()));
            structure->set(x, y, z, states[cells[cell]]);
        }
    }

    structure->mobs = base.mobs;
    for (QcStructure::Mob &mob : structure->mobs) {
        mob.mob = mobSwaps.value(mob.mob, mob.mob);
    }
    structure->stands = base.stands;
    structure->contents = base.contents;
    structure->labels = base.labels;
    structure->updates = base.updates;
    structure->growing = base.growing;
    structure->minecarts = base.minecarts;
    return structure;
}
