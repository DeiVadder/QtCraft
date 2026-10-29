// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/admin/QcAdminCommandList.h"

#include <QCoreApplication>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

// The registry's texts are keys of "QcServerMessages", a macro's plain texts.
QString displayed(const QString &text, bool macro)
{
    return macro ? text : QCoreApplication::translate("QcServerMessages", text.toUtf8().constData());
}

} // namespace

QString QcAdminCommandList::groupTitle(QcCommandCategory category)
{
    switch (category) {
    case QcCommandCategory::General:
        return tr("General");
    case QcCommandCategory::Players:
        return tr("Players");
    case QcCommandCategory::Items:
        return tr("Items and health");
    case QcCommandCategory::Creatures:
        return tr("Creatures");
    case QcCommandCategory::Building:
        return tr("Building");
    case QcCommandCategory::World:
        return tr("World and rules");
    case QcCommandCategory::Finding:
        return tr("Finding places");
    case QcCommandCategory::Server:
        return tr("Server");
    case QcCommandCategory::Other:
        break;
    }
    return tr("More commands");
}

QcAdminCommandList::QcAdminCommandList(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QcAdminCommandList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant QcAdminCommandList::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }

    const Entry &entry = shown()[index.row()];
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return entry.name;
    case UsageRole:
        return entry.usage;
    case DescriptionRole:
        return entry.description;
    case GroupRole:
        return entry.groupTitle;
    case OperatorRole:
        return entry.operatorOnly;
    default:
        return {};
    }
}

QHash<int, QByteArray> QcAdminCommandList::roleNames() const
{
    return {{NameRole, "name"}, {UsageRole, "usage"}, {DescriptionRole, "description"}, {GroupRole, "group"}, {OperatorRole, "operatorOnly"}};
}

void QcAdminCommandList::setFilter(const QString &filter)
{
    // The same filter again changes nothing.
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    refilter();
    emit filterChanged();
}

void QcAdminCommandList::setCatalogue(const QcMsg::AdminCatalogue &catalogue)
{
    QList<Entry> entries;
    for (const QcMsg::AdminCatalogue::Command &command : catalogue.commands) {
        const QcCommandCategory category = QcCommandCategories::fromWire(command.category).value_or(QcCommandCategory::Other);
        const QString title = groupTitle(category);
        entries.append({command.name, command.aliases, displayed(command.usage, command.macro), displayed(command.description, command.macro),
                        static_cast<int>(category), title, command.permission > 0});
    }
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) { return std::tie(a.group, a.name) < std::tie(b.group, b.name); });
    m_entries = std::move(entries);
    refilter();
}

bool QcAdminCommandList::matches(const Entry &entry) const
{
    const QString typed = filter().trimmed();
    const QString needle = typed.startsWith(u'/') ? typed.mid(1) : typed;
    if (needle.isEmpty()) {
        return true;
    }
    const auto has = [&needle](const QString &text) { return text.contains(needle, Qt::CaseInsensitive); };
    return has(entry.name) || std::ranges::any_of(entry.aliases, has) || has(entry.usage) || has(entry.description) || has(entry.groupTitle);
}

void QcAdminCommandList::refilter()
{
    QList<Entry> kept;
    std::ranges::copy_if(entries(), std::back_inserter(kept), [this](const Entry &entry) { return matches(entry); });
    beginResetModel();
    m_shown = std::move(kept);
    endResetModel();
    emit countChanged();
}
