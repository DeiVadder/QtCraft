// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"
#include "common/content/registry/QcNamedList.h"

#include <QStringList>

class QcContentRegistry;

/*! \brief The advancements of the content (part of QcContentRegistry, decision E-923), in load
    order and replaced by name like other definitions.

    A pack switches one off with \c {"disabled": true}; resolve() then takes it out and hangs its
    children on its parent, so a mod can drop a step of the base tree without cutting it. */
class QcAdvancementCatalog
{
public:
    /*! The most advancements a content holds (the content sync and the player files rely on it). */
    static constexpr qsizetype maxAdvancements = 1024;

    /*! Adds \a advancement, replacing one of the same name. A switched-off advancement keeps the
        parent of the one it replaces, so resolve() can hang its children there. */
    void add(QcAdvancementDefinition advancement)
    {
        if (const QcAdvancementDefinition *earlier = m_advancements.find(advancement.name); earlier && advancement.disabled) {
            advancement.parent = earlier->parent;
        }
        m_advancements.set(std::move(advancement));
    }
    /*! Returns the advancements in load order. */
    [[nodiscard]] const QList<QcAdvancementDefinition> &items() const { return m_advancements.items(); }
    /*! Returns the advancement named \a name, or null if there is none. */
    [[nodiscard]] const QcAdvancementDefinition *find(const QString &name) const { return m_advancements.find(name); }
    /*! Returns the position of the advancement named \a name in items(); none if there is none. */
    [[nodiscard]] std::optional<qsizetype> indexOf(const QString &name) const { return m_advancements.indexOf(name); }

    /*! Once all packs are loaded (QcContentRegistry::finish): removes the switched-off
        advancements, keeps at most maxAdvancements, makes an advancement whose parent is unknown
        or part of a loop a root, and checks the rest against \a content: icons, handbook links,
        English texts and conditions naming nothing of the content (such a criterion never
        matches). Returns the warnings. */
    [[nodiscard]] QStringList resolve(const QcContentRegistry &content);

private:
    void removeDisabled();
    QStringList cutLoops();

    QcNamedList<QcAdvancementDefinition> m_advancements;  // NOLINT(qc-member-access): the whole state of the class; its methods are the accessors
};
