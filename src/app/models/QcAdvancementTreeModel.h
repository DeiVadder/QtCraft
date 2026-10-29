// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/player/QcAdvancementTree.h"
#include "common/gameplay/QcAdvancementProgress.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <utility>

class QcContentRegistry;
class QcSettingsController;
struct QcSessionParts;

// The tree view of the adventure guide for QML (GameController.advancements, decision E-924): one
// tab per shown root, the shown advancements of the chosen tab with their places (columns and rows
// of QcAdvancementTree, the screen multiplies them by its steps), texts in the content language,
// frame and state, the progress count of a partly reached one ("2/4") and its criteria. update()
// follows the session's progress; the screen calls it again when it opens (the language may have
// changed since). The chosen tab and the chosen advancement are kept by name, so a change of the
// progress keeps both; `changed` is emitted only when something the screen shows differs.
class QcAdvancementTreeModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdvancementTree)
    QML_UNCREATABLE("Provided by GameController.advancements")

    // [{name, title, icon}] of the shown roots.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY changed FINAL)
    // The chosen tab's index in `tabs`.
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY changed FINAL)
    // [{name, column, row, parent, parentColumn, parentRow, title, description, icon, frame, state,
    // progress, handbook, criteria: [{text, met}]}] of the chosen tab, the root first; `parent` is an
    // index into this list (-1 for the root, then parentColumn and parentRow are 0), `progress` "2/4"
    // for a partly reached one, else empty, `criteria` only for an advancement with more than one
    // criterion. Each read converts the whole list: QML reads it once and keeps it.
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed FINAL)
    // The chosen tree's extent in columns and rows.
    Q_PROPERTY(int columns READ columns NOTIFY changed FINAL)
    Q_PROPERTY(double rows READ rows NOTIFY changed FINAL)
    // Reached advancements of the content and all of them.
    Q_PROPERTY(int reached READ reached NOTIFY changed FINAL)
    Q_PROPERTY(int total READ total NOTIFY changed FINAL)
    // The name of the chosen advancement (under the pointer or with the focus; empty for none), and
    // its index in `nodes` (-1 while it is not in the chosen tab).
    Q_PROPERTY(QString selected READ selected WRITE setSelected NOTIFY selectedChanged FINAL)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedChanged FINAL)

public:
    // How an advancement is framed (QcAdvancementFrame).
    enum class Frame { Task, Goal, Challenge };
    Q_ENUM(Frame)
    // Reached, partly reached or not yet (QcAdvancementTree::State).
    enum class State { Open, Partial, Reached };
    Q_ENUM(State)

    QcAdvancementTreeModel(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    const QVariantList &tabs() const { return m_tabs; }
    int tab() const { return m_tab; }
    void setTab(int tab);
    QVariantList nodes() const { return current().nodes; }
    int columns() const { return current().columns; }
    double rows() const { return current().rows; }
    int reached() const { return m_reached; }
    int total() const { return m_total; }
    QString selected() const { return m_selected; }
    void setSelected(const QString &name);
    int selectedIndex() const;

    // Reads the progress and the content of the session again.
    Q_INVOKABLE void update();
    // Shows `progress` over the advancements of `content` (none without content); update() with the
    // session's, tests with their own.
    void show(const QcContentRegistry *content, const QcAdvancementProgress &progress);

signals:
    void changed();
    void selectedChanged();

private:
    // What the screen shows of one tab.
    struct Shown
    {
        QVariantList nodes;
        int columns = 0;
        double rows = 0.0;

        bool operator==(const Shown &) const = default;
    };

    QVariantMap nodeMap(const QcContentRegistry &content, const QcAdvancementProgress &progress, const QcAdvancementTree::Tree &tree,
                        qsizetype index) const;
    // The criteria of `advancement` with a text each; empty for one with a single criterion.
    QVariantList criteriaOf(const QcContentRegistry &content, const QcAdvancementProgress &progress, const QcAdvancementDefinition &advancement) const;
    // The name of the item or creature a criterion is named after, else its name with spaces.
    QString criterionText(const QcContentRegistry &content, const QString &advancement, const QString &criterion) const;
    QString text(const QcContentRegistry &content, const QString &key) const;
    // The nodes of `tree` as the screen shows them.
    Shown shownTree(const QcContentRegistry &content, const QcAdvancementProgress &progress, const QcAdvancementTree::Tree &tree) const;
    // The index of the tab whose root is `root` in `tabs`; the first one when it is not there.
    static int tabOf(const QVariantList &tabs, const QString &root);
    // The chosen tab's nodes (none without tabs).
    const Shown &current() const;
    std::shared_ptr<const QcContentRegistry> content() const;
    void setTabs(QVariantList tabs) { m_tabs = std::move(tabs); }
    const QList<Shown> &shown() const { return m_shown; }
    void setShown(QList<Shown> shown) { m_shown = std::move(shown); }
    const QString &tabRoot() const { return m_tabRoot; }
    void setTabRoot(QString root) { m_tabRoot = std::move(root); }

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QVariantList m_tabs;
    // What each tab shows, in the order of m_tabs.
    QList<Shown> m_shown;
    int m_tab = 0;
    // The chosen tab's root: the tab follows it when roots show up or go away.
    QString m_tabRoot;
    int m_reached = 0;
    int m_total = 0;
    QString m_selected;
};
