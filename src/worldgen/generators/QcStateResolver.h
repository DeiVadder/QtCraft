// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QStringList>

#include <vector>

/*! \brief Looks up the block states a generated structure places, by name, and remembers the ones
    the content lacks: a structure with a missing block is not built (mine shafts, fortresses). */
class QcStateResolver
{
public:
    /*! Looks up in \a content, which outlives it; a warning names \a structure (for the log) and
        what then is \a missing ("no mine shafts"). */
    QcStateResolver(const QcContentRegistry &content, QString structure, QString missing);

    /*! Returns the state \a name, or air when the content has none (then resolved() turns
        false). */
    [[nodiscard]] QcBlockId state(const QString &name);
    /*! Returns whether every state asked for so far was found. */
    [[nodiscard]] bool resolved() const { return m_resolved; }
    /*! Returns a warning for each state that was not found. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }
    /*! Returns for every block state of the content whether \a test holds for its block. */
    template <typename Test>
    [[nodiscard]] std::vector<bool> everyState(Test test) const
    {
        std::vector<bool> states(static_cast<std::size_t>(m_content.stateCount()));
        for (std::size_t id = 0; id < states.size(); ++id) {
            states[id] = test(m_content.block(static_cast<QcBlockId>(id)));
        }
        return states;
    }
    /*! Returns for every block state whether it offers nothing to stand on (not solid, or
        liquid): what a pier or a support passes on its way down. */
    [[nodiscard]] std::vector<bool> openStates() const
    {
        return everyState([](const QcBlockDefinition &block) { return !block.solid || block.liquid; });
    }

private:
    const QcContentRegistry &m_content;
    QString m_structure;
    QString m_missing;
    QStringList m_warnings;
    bool m_resolved = true;
};
