// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcHandbookNavigation.h"

#include "app/debug/QcAppLogging.h"
#include "app/models/QcHandbookModel.h"

#include <utility>

using namespace Qt::StringLiterals;

QcHandbookNavigation::QcHandbookNavigation(const QcHandbookModel &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    connect(&m_model, &QcHandbookModel::handbookChanged, this, &QcHandbookNavigation::refresh);
}

int QcHandbookNavigation::firstEntryOf(const QVariantList &rows)
{
    for (qsizetype row = 0; row < rows.size(); ++row) {
        if (rows[row].toMap().contains(u"id"_s)) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int QcHandbookNavigation::categoryIndex() const
{
    if (searching()) {
        return -1;
    }

    const QVariantList categories = m_model.categories();
    for (qsizetype index = 0; index < categories.size(); ++index) {
        if (categories[index].toMap().value(u"id"_s).toString() == category()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int QcHandbookNavigation::firstEntry() const
{
    return firstEntryOf(listed());
}

int QcHandbookNavigation::currentRow() const
{
    const QVariantList rows = listed();
    for (qsizetype row = 0; row < rows.size(); ++row) {
        if (rows[row].toMap().value(u"id"_s).toString() == current()) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

void QcHandbookNavigation::setSearch(const QString &search)
{
    setListing(category(), search);
}

void QcHandbookNavigation::setListing(const QString &category, const QString &search)
{
    // The same category and search again: the list stays (the field sets it on every edit).
    if (category == m_category && search == m_search) {
        return;
    }

    m_category = category;
    m_search = search;
    updateList();
}

void QcHandbookNavigation::updateList()
{
    m_listed = searching() ? m_model.search(search()) : m_model.entries(category());
    emit listChanged();
    emit currentRowChanged();
}

void QcHandbookNavigation::refresh()
{
    if (current().isEmpty()) {
        updateList();
        return;
    }

    // The open page went away with the rebuild (a mod removed): the start page instead.
    if (m_model.resolve(current()).isEmpty()) {
        const QString start = startPage({}, {});
        if (start.isEmpty()) {
            setCurrent({});
        } else {
            show(start);
        }
    } else {
        setCurrent(current());
    }
    updateList();
}

void QcHandbookNavigation::start(const QString &target, const QString &query)
{
    setHistory({});
    setSearch(query);
    const QString first = startPage(target, query);
    if (!first.isEmpty()) {
        show(first);
    }
}

QString QcHandbookNavigation::startPage(const QString &target, const QString &query) const
{
    QString resolved = m_model.resolve(target);
    if (!resolved.isEmpty()) {
        return resolved;
    }

    const QVariantList found = query.trimmed().isEmpty() ? QVariantList() : m_model.search(query);
    const int foundRow = firstEntryOf(found);
    if (foundRow >= 0) {
        return found[foundRow].toMap().value(u"id"_s).toString();
    }

    QString shown = m_model.resolve(current());
    if (!shown.isEmpty()) {
        return shown;
    }

    const QVariantList playing = m_model.entries(startCategory.toString());
    const int playingRow = firstEntryOf(playing);
    return playingRow >= 0 ? playing[playingRow].toMap().value(u"id"_s).toString() : QString();
}

void QcHandbookNavigation::navigate(const QString &target, bool remember)
{
    const QString id = m_model.resolve(target);
    if (id.isEmpty()) {
        qCWarning(lcGame) << "Handbook: no page for" << target;
        return;
    }
    // The open page again (a link to itself, its own row clicked): it stays as it is.
    if (id == current()) {
        return;
    }

    if (remember && !current().isEmpty()) {
        setHistory(history() + QStringList{current()});
    }
    show(id);
}

void QcHandbookNavigation::back()
{
    // Nothing to return to: the button is disabled then, Backspace still asks.
    if (history().isEmpty()) {
        return;
    }

    // Pages that went away with a rebuild (a mod removed) are skipped.
    QStringList pages = history();
    QString previous;
    while (!pages.isEmpty() && previous.isEmpty()) {
        previous = m_model.resolve(pages.takeLast());
    }
    setHistory(std::move(pages));
    if (!previous.isEmpty()) {
        show(previous);
    }
}

void QcHandbookNavigation::openRow(int row)
{
    if (row < 0 || row >= listed().size()) {
        qCWarning(lcGame) << "Handbook: no entry row" << row << "of" << listed().size();
        return;
    }
    // A pack's header row opens nothing.
    const QVariantMap entry = listed()[row].toMap();
    if (entry.contains(u"id"_s)) {
        navigate(entry.value(u"id"_s).toString(), true);
    }
}

void QcHandbookNavigation::chooseCategory(int index)
{
    // -1: the side bar marks no category while search results show.
    if (index == -1) {
        return;
    }
    const QVariantList categories = m_model.categories();
    if (index < 0 || index >= categories.size()) {
        qCWarning(lcGame) << "Handbook: no category" << index << "of" << categories.size();
        return;
    }

    setListing(categories[index].toMap().value(u"id"_s).toString(), QString());
}

void QcHandbookNavigation::show(const QString &id)
{
    setCurrent(id);
    if (!searching()) {
        setListing(page().value(u"category"_s).toString(), search());
    }
    emit currentRowChanged();
    emit pageShown();
}

void QcHandbookNavigation::setCurrent(const QString &id)
{
    m_current = id;
    m_page = m_model.page(id);
    emit pageChanged();
}

void QcHandbookNavigation::setHistory(QStringList history)
{
    const bool before = canGoBack();
    m_history = std::move(history);
    if (canGoBack() != before) {
        emit historyChanged();
    }
}
