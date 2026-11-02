// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbook.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

// The handbook for QML: category and entry lists, search and pages. Pages are converted on request
// (QcHandbookJson), so only what is shown costs anything; the handbook itself is built once per
// content and language by QcHandbookController.
class QcHandbookModel : public QObject
{
    Q_OBJECT
    // HandbookModel in QML, for its enumeration (HandbookModel.RecipeKind.Machine).
    QML_NAMED_ELEMENT(HandbookModel)
    QML_UNCREATABLE("Provided by GameController.handbook.model")
    // [{id, title, count}] of the non-empty categories.
    Q_PROPERTY(QVariantList categories READ categories NOTIFY handbookChanged FINAL)

public:
    // The kind of a recipe on a page (QcHandbookEntry::Recipe::Kind, with the same values).
    enum class RecipeKind {
        Crafting,
        Machine
    };
    Q_ENUM(RecipeKind)

    explicit QcHandbookModel(QObject *parent = nullptr);

    void setHandbook(std::shared_ptr<const QcHandbook> handbook);
    const QcHandbook *handbook() const { return m_handbook.get(); }
    QVariantList categories() const { return m_categories; }

    // [{id, title, icon, category, pack, packName}] of one category, grouped by pack; with mods
    // loaded a {header: <pack name>} row starts each pack.
    Q_INVOKABLE QVariantList entries(const QString &category) const;
    // The same for the best matches of `text`, with "categoryTitle".
    Q_INVOKABLE QVariantList search(const QString &text) const;
    // A whole page (see QcHandbookJson::entry), each recipe's "kind" a RecipeKind; empty for
    // unknown ids.
    Q_INVOKABLE QVariantMap page(const QString &id) const;
    // Entry id for an entry id or a content name (block, item, creature, biome, chapter; unqualified
    // names in any pack); empty when none.
    Q_INVOKABLE QString resolve(const QString &target) const;

signals:
    void handbookChanged();

private:
    QVariantMap summary(const QcHandbookEntry &entry) const;

    std::shared_ptr<const QcHandbook> m_handbook;
    QVariantList m_categories;
};
