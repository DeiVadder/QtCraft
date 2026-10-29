// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/structures/QcStructure.h"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

// A village blueprint with its variation resolved (decision E-404): the structure as it is, each
// choice's options as palette overrides and creature swaps, and the optional parts. Builds the
// structure of one placed copy from a selection the planner draws from the seed. Immutable after
// resolving, so the generator's threads share it.
class QcPieceVariation
{
public:
    // The option taken of every choice and whether each part is built.
    struct Selection
    {
        QList<int> options;
        QList<bool> parts;
    };

    // `definition` already carries the style's palette swaps. Unknown blocks of the structure fail;
    // an option naming an unknown block keeps that palette entry with a warning (warnings()).
    // A failure carries the warnings of the palette entries before the unknown block.
    using Resolved = std::expected<QcPieceVariation, QcStructure::Failure>;

    [[nodiscard]] static Resolved resolve(const QcStructureDefinition &definition, const QcContentRegistry &content);
    // The warnings of a resolution, whether it succeeded or not (callers report them before the outcome).
    [[nodiscard]] static const QStringList &warningsOf(const Resolved &resolved)
    {
        return resolved ? resolved->warnings() : resolved.error().warnings;
    }

    // What resolving kept at a default or left out (unknown states, unknown blocks of options).
    const QStringList &warnings() const { return m_warnings; }

    // The structure without parts and with every palette entry as written.
    const std::shared_ptr<const QcStructure> &base() const { return m_base; }
    bool varies() const { return !m_choices.isEmpty() || !m_parts.isEmpty(); }
    // How many options each choice has, and the parts' chances.
    [[nodiscard]] QList<int> optionCounts() const;
    [[nodiscard]] QList<double> partChances() const;
    // A selection from `random` (values in [0, 1)): one draw per choice, then one per part.
    [[nodiscard]] Selection choose(const std::function<double()> &random) const;
    // Every part built and the first option of every choice (the showcase shows this one).
    [[nodiscard]] Selection everything() const;
    // The structure of a selection; the base itself when nothing varies.
    [[nodiscard]] std::shared_ptr<const QcStructure> build(const Selection &selection) const;

private:
    struct Option
    {
        // Definition palette index -> state.
        QList<std::pair<qsizetype, QcBlockId>> states;
        QHash<QString, QString> mobs;
    };

    QcPieceVariation() = default;
    // One option of a choice; its unknown blocks go to the warnings.
    [[nodiscard]] Option resolveOption(const QcStructureDefinition &definition, const QHash<QString, QString> &swaps,
                                       const QcContentRegistry &content);

    std::shared_ptr<const QcStructure> m_base;
    // The definition's palette resolved, and its cells (QcStructureDefinition::cellIndex).
    QList<QcBlockId> m_states;
    QList<qint16> m_cells;
    // The definition's palette as written (which entries set their connections themselves) and the
    // registry the connections of a built copy come from; the registry outlives the variations
    // resolved against it (the world generator's content).
    QStringList m_paletteEntries;
    const QcContentRegistry *m_content = nullptr;
    QStringList m_warnings;
    QList<QList<Option>> m_choices;
    QList<QcStructureDefinition::Part> m_parts;
};
