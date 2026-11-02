// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QcHandbookModel;

// Where the reader is in the handbook (GameController.handbook.navigation, decision E-954): the open
// page and the pages before it ("Previous page"), the chosen category or the search, and the entry
// list they give. It lives with the controller, so the screen shows the same page again after it was
// unloaded (LazyScreen, decision E-691) and follows a rebuilt handbook (language, mods); the screen
// keeps only the focus and the scrolling.
class QcHandbookNavigation : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HandbookNavigation)
    QML_UNCREATABLE("Provided by GameController.handbook.navigation")
    // The open page's entry id (empty before the first one) and the page (QcHandbookModel::page).
    Q_PROPERTY(QString current READ current NOTIFY pageChanged FINAL)
    Q_PROPERTY(QVariantMap page READ page NOTIFY pageChanged FINAL)
    // Whether back() has a page to return to.
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged FINAL)
    // The chosen category's id; a page opened outside a search chooses its category.
    Q_PROPERTY(QString category READ category NOTIFY listChanged FINAL)
    // The search text; the entry list shows its results while it holds more than spaces.
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY listChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY listChanged FINAL)
    // The chosen category's index in the model's categories; -1 while searching.
    Q_PROPERTY(int categoryIndex READ categoryIndex NOTIFY listChanged FINAL)
    // The entry list: the search results or the chosen category's entries (QcHandbookModel rows,
    // pack headers included).
    Q_PROPERTY(QVariantList listed READ listed NOTIFY listChanged FINAL)
    // The row of the first entry in `listed` (not a pack header), -1 for none.
    Q_PROPERTY(int firstEntry READ firstEntry NOTIFY listChanged FINAL)
    // The open page's row in `listed`, -1 when it is not listed.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged FINAL)

public:
    // The category whose first entry a handbook opened for the first time shows.
    static constexpr QStringView startCategory = u"playing";

    explicit QcHandbookNavigation(const QcHandbookModel &model, QObject *parent = nullptr);

    QString current() const { return m_current; }
    QVariantMap page() const { return m_page; }
    bool canGoBack() const { return !history().isEmpty(); }
    QString category() const { return m_category; }
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    bool searching() const { return !search().trimmed().isEmpty(); }
    int categoryIndex() const;
    const QVariantList &listed() const { return m_listed; }
    int firstEntry() const;
    int currentRow() const;

    // The handbook opens: the search becomes `query`, the history is forgotten and startPage() shows.
    void start(const QString &target, const QString &query);
    // The page a handbook opening with `target` (an entry id or content name) and `query` shows: the
    // target, else the first search result, else the page shown last, else the first entry of
    // startCategory. Empty only for an empty handbook.
    QString startPage(const QString &target, const QString &query) const;

    // Opens the page of `target` (an entry id or content name); `remember` keeps the open page for
    // back(). The open page again changes nothing.
    Q_INVOKABLE void navigate(const QString &target, bool remember = true);
    // Returns to the page before, skipping pages a rebuilt handbook no longer has (none: nothing
    // happens).
    Q_INVOKABLE void back();
    // Opens the entry in row `row` of `listed`, remembering the open page.
    Q_INVOKABLE void openRow(int row);
    // Chooses the category at `index` of the model's categories (the side bar) and ends a search.
    Q_INVOKABLE void chooseCategory(int index);

signals:
    void pageChanged();
    // A page was shown (also the open one again): the screen scrolls it to the top and marks its row.
    void pageShown();
    void historyChanged();
    void listChanged();
    void currentRowChanged();

private:
    // Shows the page of entry `id` even when it is open already.
    void show(const QString &id);
    // Sets the category and the search together and lists their entries once.
    void setListing(const QString &category, const QString &search);
    // The entries of the category or the search again.
    void updateList();
    // The handbook was rebuilt: page and list again from it; a page it no longer has gives way to
    // the start page.
    void refresh();
    // The row of the first entry in `rows`, -1 for none.
    static int firstEntryOf(const QVariantList &rows);
    const QStringList &history() const { return m_history; }
    void setHistory(QStringList history);
    void setCurrent(const QString &id);

    const QcHandbookModel &m_model;
    QString m_current;
    QVariantMap m_page;
    QStringList m_history;
    QString m_category;
    QString m_search;
    QVariantList m_listed;
};
