// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcAdvancementTreeModel.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

QcAdvancementTreeModel::QcAdvancementTreeModel(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
}

void QcAdvancementTreeModel::setTab(int tab)
{
    // The same tab again changes nothing (the tabs set it on every click).
    if (tab == this->tab()) {
        return;
    }
    if (tab < 0 || tab >= tabs().size()) {
        qCWarning(lcGame) << "Ignoring advancement tab" << tab << ": there are" << tabs().size() << "tabs";
        return;
    }

    m_tab = tab;
    setTabRoot(tabs()[tab].toMap().value(u"name"_s).toString());
    emit changed();
    emit selectedChanged();
}

void QcAdvancementTreeModel::setSelected(const QString &name)
{
    // The same one again: the pointer and the focus both choose it.
    if (name == m_selected) {
        return;
    }

    m_selected = name;
    emit selectedChanged();
}

int QcAdvancementTreeModel::selectedIndex() const
{
    if (selected().isEmpty()) {
        return -1;
    }

    const QVariantList &shown = current().nodes;
    for (qsizetype index = 0; index < shown.size(); ++index) {
        if (shown[index].toMap().value(u"name"_s).toString() == selected()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::shared_ptr<const QcContentRegistry> QcAdvancementTreeModel::content() const
{
    return m_parts.session ? m_parts.session->content() : nullptr;
}

const QcAdvancementTreeModel::Shown &QcAdvancementTreeModel::current() const
{
    static const Shown none;
    return tab() < shown().size() ? shown()[tab()] : none;
}

void QcAdvancementTreeModel::update()
{
    const std::shared_ptr<const QcContentRegistry> registry = content();
    show(registry.get(), registry ? m_parts.session->advancements().progress() : QcAdvancementProgress{});
}

void QcAdvancementTreeModel::show(const QcContentRegistry *content, const QcAdvancementProgress &progress)
{
    QVariantList tabs;
    QList<Shown> shown;
    int reached = 0;
    int total = 0;
    if (content) {
        const QList<QcAdvancementDefinition> &advancements = content->advancements().items();
        total = static_cast<int>(advancements.size());
        reached = static_cast<int>(std::ranges::count_if(advancements, [&progress](const auto &advancement) { return progress.isDone(advancement); }));
        const QList<QcAdvancementTree::Tree> trees = QcAdvancementTree::trees(advancements, progress);
        for (const QcAdvancementTree::Tree &tree : trees) {
            const QcAdvancementDefinition &root = advancements[tree.root];
            tabs.append(QVariantMap{{u"name"_s, root.name}, {u"title"_s, text(*content, root.titleKey)}, {u"icon"_s, root.icon}});
            shown.append(shownTree(*content, progress, tree));
        }
    }

    // The tab stays on its root, wherever that went; a root no longer shown falls back to the first.
    const int tab = tabOf(tabs, tabRoot());
    const bool same = tabs == this->tabs() && tab == this->tab() && reached == this->reached() && total == this->total()
                      && (tab < shown.size() ? shown[tab] : Shown{}) == current();
    if (!tabs.isEmpty()) {
        setTabRoot(tabs[tab].toMap().value(u"name"_s).toString());
    }
    setTabs(std::move(tabs));
    setShown(std::move(shown));
    m_tab = tab;
    m_reached = reached;
    m_total = total;
    // Nothing the screen shows changed: its delegates, focus and scroll stay as they are.
    if (same) {
        return;
    }
    emit changed();
    emit selectedChanged();
}

int QcAdvancementTreeModel::tabOf(const QVariantList &tabs, const QString &root)
{
    for (qsizetype index = 0; index < tabs.size(); ++index) {
        if (tabs[index].toMap().value(u"name"_s).toString() == root) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

QcAdvancementTreeModel::Shown QcAdvancementTreeModel::shownTree(const QcContentRegistry &content, const QcAdvancementProgress &progress,
                                                                const QcAdvancementTree::Tree &tree) const
{
    Shown shown{{}, tree.columns, tree.rows};
    for (qsizetype index = 0; index < tree.nodes.size(); ++index) {
        shown.nodes.append(nodeMap(content, progress, tree, index));
    }
    return shown;
}

QVariantMap QcAdvancementTreeModel::nodeMap(const QcContentRegistry &content, const QcAdvancementProgress &progress, const QcAdvancementTree::Tree &tree,
                                            qsizetype index) const
{
    const QcAdvancementTree::Node &node = tree.nodes[index];
    const QcAdvancementDefinition &advancement = content.advancements().items()[node.advancement];
    const bool partial = node.state == QcAdvancementTree::State::Partial;
    // The parent's place travels with the child: a line reads it without looking the parent up.
    const QcAdvancementTree::Node *parent = node.parent >= 0 ? &tree.nodes[node.parent] : nullptr;
    return {{u"name"_s, advancement.name},
            {u"column"_s, node.column},
            {u"row"_s, node.row},
            {u"parent"_s, static_cast<int>(node.parent)},
            {u"parentColumn"_s, parent ? parent->column : 0},
            {u"parentRow"_s, parent ? parent->row : 0.0},
            {u"title"_s, text(content, advancement.titleKey)},
            {u"description"_s, text(content, advancement.descriptionKey)},
            {u"icon"_s, advancement.icon},
            {u"frame"_s, static_cast<int>(advancement.frame)},
            {u"state"_s, static_cast<int>(node.state)},
            {u"progress"_s, partial && node.groups > 1 ? u"%1/%2"_s.arg(node.groupsMet).arg(node.groups) : QString()},
            {u"handbook"_s, advancement.handbook},
            {u"criteria"_s, criteriaOf(content, progress, advancement)}};
}

QVariantList QcAdvancementTreeModel::criteriaOf(const QcContentRegistry &content, const QcAdvancementProgress &progress,
                                                const QcAdvancementDefinition &advancement) const
{
    if (advancement.criteria.size() < 2) {
        return {};
    }

    const QSet<QString> met = progress.criteria(advancement.name);
    QVariantList criteria;
    for (const QcAdvancementCriterion &criterion : advancement.criteria) {
        criteria.append(QVariantMap{{u"text"_s, criterionText(content, advancement.name, criterion.name)}, {u"met"_s, met.contains(criterion.name)}});
    }
    return criteria;
}

QString QcAdvancementTreeModel::criterionText(const QcContentRegistry &content, const QString &advancement, const QString &criterion) const
{
    // A criterion named after an item or a creature of the advancement's pack shows its name.
    const QString name = advancement.section(u':', 0, 0) + u':' + criterion;
    if (const std::optional<QcItemId> item = content.itemId(name)) {
        return text(content, content.item(*item).translationKey);
    }
    if (const std::optional<int> mob = content.mobIndex(name)) {
        return text(content, content.mobs()[*mob].translationKey);
    }
    QString words = criterion;
    words.replace(u'_', u' ');
    return words;
}

QString QcAdvancementTreeModel::text(const QcContentRegistry &content, const QString &key) const
{
    return content.translations().translate(key, m_settings.contentLanguage());
}
