// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"
#include "common/content/registry/QcNamedList.h"

#include <QStringList>

class QcContentRegistry;

// The advancements of the content (part of QcContentRegistry, decision E-923), in load order and
// replaced by name like other definitions. A pack switches one off with `"disabled": true`; resolve()
// then takes it out and hangs its children on its parent, so a mod can drop a step of the base tree
// without cutting it.
class QcAdvancementCatalog
{
public:
    // The most advancements a content holds (the content sync and the player files rely on it).
    static constexpr qsizetype maxAdvancements = 1024;

    // A switched-off advancement keeps the parent of the one it replaces, so resolve() can hang its
    // children there.
    void add(QcAdvancementDefinition advancement)
    {
        if (const QcAdvancementDefinition *earlier = m_advancements.find(advancement.name); earlier && advancement.disabled) {
            advancement.parent = earlier->parent;
        }
        m_advancements.set(std::move(advancement));
    }
    [[nodiscard]] const QList<QcAdvancementDefinition> &items() const { return m_advancements.items(); }
    [[nodiscard]] const QcAdvancementDefinition *find(const QString &name) const { return m_advancements.find(name); }
    [[nodiscard]] std::optional<qsizetype> indexOf(const QString &name) const { return m_advancements.indexOf(name); }

    // Once all packs are loaded (QcContentRegistry::finish): removes the switched-off advancements,
    // makes an advancement whose parent is unknown or part of a loop a root, keeps at most
    // maxAdvancements, and warns about icons, handbook links, translations and conditions naming
    // nothing of the content (such a criterion never matches). The warnings.
    [[nodiscard]] QStringList resolve(const QcContentRegistry &content);

private:
    void removeDisabled();
    QStringList cutLoops();

    QcNamedList<QcAdvancementDefinition> m_advancements;  // NOLINT(qc-member-access): the whole state of the class; its methods are the accessors
};
