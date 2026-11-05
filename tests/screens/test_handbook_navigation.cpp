// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcHandbookNavigation.h"
#include "app/models/QcHandbookModel.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// Where the reader is in the handbook (decision E-954): the start page an opening shows, the history
// behind "Previous page", the category a page chooses outside a search, the search and the side bar,
// and the entry list with the open page's row, on a small handbook of two categories.
class TestHandbookNavigation : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcHandbookModel> m_model;
    std::unique_ptr<QcHandbookNavigation> m_navigation;

    static QcHandbookEntry entry(const QString &id, const QString &name, const QString &category, const QString &title)
    {
        QcHandbookEntry entry;
        entry.id = id;
        entry.name = name;
        entry.category = category;
        entry.title = title;
        entry.englishTitle = title;
        entry.pack = u"t"_s;
        return entry;
    }

    // "playing": two chapters; "blocks": stone and sand.
    static std::shared_ptr<const QcHandbook> handbook()
    {
        QcHandbook::Contents contents;
        contents.packs = {{u"t"_s, u"Test"_s, u"1"_s}};
        contents.entries = {entry(u"chapter:t:start"_s, u"t:start"_s, u"playing"_s, u"Getting started"_s),
                            entry(u"chapter:t:food"_s, u"t:food"_s, u"playing"_s, u"Food"_s),
                            entry(u"block:t:stone"_s, u"t:stone"_s, u"blocks"_s, u"Stone"_s),
                            entry(u"block:t:sand"_s, u"t:sand"_s, u"blocks"_s, u"Sand"_s)};
        contents.categories = {{u"playing"_s, u"Playing"_s, {0, 1}}, {u"blocks"_s, u"Blocks"_s, {2, 3}}};
        return std::make_shared<const QcHandbook>(std::move(contents));
    }

    QStringList listedIds() const
    {
        QStringList ids;
        for (const QVariant &row : m_navigation->listed()) {
            ids.append(row.toMap().value(u"id"_s).toString());
        }
        return ids;
    }

private slots:
    void init()
    {
        m_model = std::make_unique<QcHandbookModel>();
        m_model->setHandbook(handbook());
        m_navigation = std::make_unique<QcHandbookNavigation>(*m_model);
    }

    void cleanup()
    {
        m_navigation.reset();
        m_model.reset();
    }

    void firstOpeningShowsTheFirstChapter()
    {
        QSignalSpy shown(m_navigation.get(), &QcHandbookNavigation::pageShown);
        m_navigation->start({}, {});
        QCOMPARE(m_navigation->current(), u"chapter:t:start"_s);
        QCOMPARE(m_navigation->page().value(u"title"_s).toString(), u"Getting started"_s);
        QCOMPARE(m_navigation->category(), u"playing"_s);
        QCOMPARE(m_navigation->categoryIndex(), 0);
        QCOMPARE(listedIds(), (QStringList{u"chapter:t:start"_s, u"chapter:t:food"_s}));
        QCOMPARE(m_navigation->currentRow(), 0);
        QCOMPARE(shown.count(), 1);
        QVERIFY(!m_navigation->canGoBack());
    }

    // The target first, then the first search result, then the page left last.
    void startPageOrder()
    {
        QCOMPARE(m_navigation->startPage(u"t:sand"_s, {}), u"block:t:sand"_s);
        QCOMPARE(m_navigation->startPage({}, u"stone"_s), u"block:t:stone"_s);
        QCOMPARE(m_navigation->startPage({}, u"nothing like it"_s), u"chapter:t:start"_s);

        m_navigation->start(u"block:t:sand"_s, {});
        QCOMPARE(m_navigation->startPage({}, {}), u"block:t:sand"_s);
        QCOMPARE(m_navigation->startPage({}, u"   "_s), u"block:t:sand"_s);
    }

    void openingAgainForgetsTheHistory()
    {
        m_navigation->start({}, {});
        m_navigation->navigate(u"t:stone"_s);
        QVERIFY(m_navigation->canGoBack());

        m_navigation->start({}, {});
        QVERIFY(!m_navigation->canGoBack());
        QCOMPARE(m_navigation->current(), u"block:t:stone"_s);
    }

    void backReturnsThroughTheHistory()
    {
        m_navigation->start({}, {});
        QSignalSpy history(m_navigation.get(), &QcHandbookNavigation::historyChanged);
        m_navigation->navigate(u"block:t:stone"_s);
        m_navigation->navigate(u"t:sand"_s);
        // Not remembered: a page that replaces the open one.
        m_navigation->navigate(u"chapter:t:food"_s, false);
        QCOMPARE(history.count(), 1);
        QCOMPARE(m_navigation->category(), u"playing"_s);

        QSignalSpy shown(m_navigation.get(), &QcHandbookNavigation::pageShown);
        QSignalSpy row(m_navigation.get(), &QcHandbookNavigation::currentRowChanged);
        m_navigation->back();
        QCOMPARE(shown.count(), 1);
        QVERIFY(row.count() >= 1);
        QCOMPARE(m_navigation->currentRow(), 0);
        QCOMPARE(m_navigation->current(), u"block:t:stone"_s);
        QCOMPARE(m_navigation->category(), u"blocks"_s);
        QVERIFY(m_navigation->canGoBack());
        m_navigation->back();
        QCOMPARE(m_navigation->current(), u"chapter:t:start"_s);
        QVERIFY(!m_navigation->canGoBack());
        QCOMPARE(history.count(), 2);

        m_navigation->back();
        QCOMPARE(m_navigation->current(), u"chapter:t:start"_s);
    }

    void theOpenPageAgainChangesNothing()
    {
        m_navigation->start({}, {});
        QSignalSpy shown(m_navigation.get(), &QcHandbookNavigation::pageShown);
        m_navigation->navigate(u"chapter:t:start"_s);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no page for"_s));
        m_navigation->navigate(u"t:unknown"_s);
        QCOMPARE(shown.count(), 0);
        QVERIFY(!m_navigation->canGoBack());
    }

    // A search keeps the category; its results list instead, and the side bar marks none.
    void searchKeepsTheCategory()
    {
        m_navigation->start({}, {});
        m_navigation->setSearch(u"sand"_s);
        QVERIFY(m_navigation->searching());
        QCOMPARE(m_navigation->categoryIndex(), -1);
        QCOMPARE(listedIds(), QStringList{u"block:t:sand"_s});
        QCOMPARE(m_navigation->currentRow(), -1);

        m_navigation->openRow(m_navigation->firstEntry());
        QCOMPARE(m_navigation->current(), u"block:t:sand"_s);
        QCOMPARE(m_navigation->category(), u"playing"_s);
        QCOMPARE(m_navigation->currentRow(), 0);

        m_navigation->setSearch(u"  "_s);
        QVERIFY(!m_navigation->searching());
        QCOMPARE(listedIds(), (QStringList{u"chapter:t:start"_s, u"chapter:t:food"_s}));
    }

    void openingWithAQueryShowsItsFirstResult()
    {
        m_navigation->start({}, u"stone"_s);
        QCOMPARE(m_navigation->search(), u"stone"_s);
        QCOMPARE(m_navigation->current(), u"block:t:stone"_s);
        QVERIFY(m_navigation->searching());
    }

    void sideBarChoosesTheCategoryAndEndsTheSearch()
    {
        m_navigation->start({}, u"stone"_s);
        m_navigation->chooseCategory(1);
        QVERIFY(!m_navigation->searching());
        QCOMPARE(m_navigation->search(), QString());
        QCOMPARE(m_navigation->category(), u"blocks"_s);
        QCOMPARE(m_navigation->categoryIndex(), 1);
        QCOMPARE(listedIds(), (QStringList{u"block:t:stone"_s, u"block:t:sand"_s}));
        QCOMPARE(m_navigation->currentRow(), 0);

        // None marked (while searching) changes nothing; past the end is refused.
        m_navigation->chooseCategory(-1);
        QCOMPARE(m_navigation->category(), u"blocks"_s);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no category 2"_s));
        m_navigation->chooseCategory(2);
        QCOMPARE(m_navigation->category(), u"blocks"_s);
    }

    void openRowRefusesRowsOutside()
    {
        m_navigation->start({}, {});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no entry row 5"_s));
        m_navigation->openRow(5);
        m_navigation->openRow(1);
        QCOMPARE(m_navigation->current(), u"chapter:t:food"_s);
        QVERIFY(m_navigation->canGoBack());
    }

    // A rebuilt handbook (language, mods) gives the open page and the list again.
    void followsARebuiltHandbook()
    {
        m_navigation->start(u"t:stone"_s, {});
        QSignalSpy page(m_navigation.get(), &QcHandbookNavigation::pageChanged);
        QSignalSpy list(m_navigation.get(), &QcHandbookNavigation::listChanged);
        QcHandbook::Contents contents;
        contents.packs = {{u"t"_s, u"Test"_s, u"1"_s}};
        contents.entries = {entry(u"block:t:stone"_s, u"t:stone"_s, u"blocks"_s, u"Stein"_s)};
        contents.categories = {{u"blocks"_s, u"Blöcke"_s, {0}}};
        m_model->setHandbook(std::make_shared<const QcHandbook>(std::move(contents)));
        QCOMPARE(page.count(), 1);
        QCOMPARE(list.count(), 1);
        QCOMPARE(m_navigation->page().value(u"title"_s).toString(), u"Stein"_s);
        QCOMPARE(listedIds(), QStringList{u"block:t:stone"_s});
        QCOMPARE(m_navigation->categoryIndex(), 0);
    }

    // A rebuild without the open page (a mod removed) shows the start page; back() skips pages the
    // handbook no longer has.
    void rebuildWithoutTheOpenPage()
    {
        m_navigation->start({}, {});
        m_navigation->navigate(u"t:stone"_s);
        m_navigation->navigate(u"t:sand"_s);
        QSignalSpy shown(m_navigation.get(), &QcHandbookNavigation::pageShown);
        QcHandbook::Contents contents;
        contents.packs = {{u"t"_s, u"Test"_s, u"1"_s}};
        contents.entries = {entry(u"chapter:t:start"_s, u"t:start"_s, u"playing"_s, u"Getting started"_s)};
        contents.categories = {{u"playing"_s, u"Playing"_s, {0}}};
        m_model->setHandbook(std::make_shared<const QcHandbook>(std::move(contents)));
        QCOMPARE(m_navigation->current(), u"chapter:t:start"_s);
        QCOMPARE(m_navigation->page().value(u"title"_s).toString(), u"Getting started"_s);
        QCOMPARE(m_navigation->category(), u"playing"_s);
        QCOMPARE(shown.count(), 1);

        // History: start, stone (gone). Stone is skipped; back() returns to start.
        QVERIFY(m_navigation->canGoBack());
        m_navigation->back();
        QCOMPARE(m_navigation->current(), u"chapter:t:start"_s);
        QVERIFY(!m_navigation->canGoBack());
    }

    void currentRowFollowsTheOpenPage()
    {
        m_navigation->start({}, {});
        QSignalSpy row(m_navigation.get(), &QcHandbookNavigation::currentRowChanged);
        m_navigation->openRow(1);
        QVERIFY(row.count() >= 1);
        QCOMPARE(m_navigation->currentRow(), 1);
    }
};

QTEST_MAIN(TestHandbookNavigation)
#include "test_handbook_navigation.moc"
