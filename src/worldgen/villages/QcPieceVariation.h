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

/*! \brief A village blueprint with its variation resolved (decision E-404): the structure as it is,
    each choice's options as palette overrides and creature swaps, and the optional parts.

    Builds the structure of one placed copy from a selection the planner draws from the seed.
    Immutable after resolving, so the generator's threads share it. */
class QcPieceVariation
{
public:
    /*! \brief The option taken of every choice and whether each part is built. */
    struct Selection
    {
        /*! The index of the option taken, one per choice. */
        QList<int> options;
        /*! Whether each part is built, one per part. */
        QList<bool> parts;
    };

    /*! The result of resolve(): the variation, or the failure of the structure. */
    using Resolved = std::expected<QcPieceVariation, QcStructure::Failure>;

    /*! Returns \a definition resolved against the blocks of \a content; \a definition already
        carries the style's palette swaps. Unknown blocks of the structure fail; an option naming
        an unknown block keeps that palette entry with a warning (warnings()). A failure carries
        the warnings of the palette entries before the unknown block. */
    [[nodiscard]] static Resolved resolve(const QcStructureDefinition &definition, const QcContentRegistry &content);
    /*! Returns the warnings of the resolution \a resolved, whether it succeeded or not (callers
        report them before the outcome). */
    [[nodiscard]] static const QStringList &warningsOf(const Resolved &resolved)
    {
        return resolved ? resolved->warnings() : resolved.error().warnings;
    }

    /*! Returns what resolving kept at a default or left out (unknown states, unknown blocks of
        options). */
    const QStringList &warnings() const { return m_warnings; }

    /*! Returns the structure without parts and with every palette entry as written. */
    const std::shared_ptr<const QcStructure> &base() const { return m_base; }
    /*! Returns whether the blueprint has choices or parts. */
    bool varies() const { return !m_choices.isEmpty() || !m_parts.isEmpty(); }
    /*! Returns how many options each choice has. */
    [[nodiscard]] QList<int> optionCounts() const;
    /*! Returns the chance of each part. */
    [[nodiscard]] QList<double> partChances() const;
    /*! Returns a selection from \a random (values in [0, 1)): one draw per choice, then one per
        part. */
    [[nodiscard]] Selection choose(const std::function<double()> &random) const;
    /*! Returns the selection with every part built and the first option of every choice (the
        showcase shows this one). */
    [[nodiscard]] Selection everything() const;
    /*! Returns the structure of \a selection: options swap palette entries and creatures,
        parts built overwrite their cells, connections follow the built copy; the base itself when
        nothing varies. */
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
