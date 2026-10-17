#include "clientcore/icons/QcItemIconSet.h"
#include "clientcore/icons/QcWebHandbookAssets.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/settings/QcUiPalette.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/gameplay/QcKeyBindings.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "common/handbook/QcHandbookWeb.h"
#include "server/gameplay/QcHandbookRules.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <cstdlib>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// web/handbook/data is `qtcraft-headless --no-mods --export-handbook-web web/handbook/data`;
// regenerate it when a comparison with it fails.
QString dataFile(const QString &name)
{
    return QDir(QStringLiteral(QTCRAFT_SOURCE_DIR)).filePath(u"web/handbook/data/"_s + name);
}

QByteArray readData(const QString &name)
{
    QFile file(dataFile(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QcHandbook build(const QcContentRegistry &content, const QString &language)
{
    return QcHandbookBuilder::build(content, language, QcHandbookRules::values(),
                                    QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
}

QByteArray readSource(const QString &path)
{
    QFile file(QDir(QStringLiteral(QTCRAFT_SOURCE_DIR)).filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The largest difference of one colour channel between two images of the same size.
int largestDifference(const QImage &a, const QImage &b)
{
    int largest = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            largest = std::max({largest, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q)),
                                std::abs(qAlpha(p) - qAlpha(q))});
        }
    }
    return largest;
}

// Queries for the search comparison: the first word and the start of every title in both
// languages, plus words with accents, ß and several words.
QStringList searchQueries(const QcHandbook &book)
{
    QSet<QString> queries{u"eisen"_s, u"Spitzhacke"_s, u"stone"_s, u"wood pick"_s, u"weiss"_s, u"weißer beton"_s, u"WEISSE wolle"_s,
                          u"  ofen  "_s, u"glas"_s, u"brennstoff"_s, u"xyz"_s, u"café"_s, u"Erze"_s};
    for (const QcHandbookEntry &entry : book.entries()) {
        queries.insert(entry.title.section(u' ', 0, 0));
        queries.insert(entry.title.left(4));
        queries.insert(entry.englishTitle.left(3));
    }
    QStringList sorted(queries.begin(), queries.end());
    sorted.sort();
    return sorted;
}

QImage solid(QColor colour, int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(colour);
    return image;
}

// The values of the array `key` of `object`, as a list a range loop can walk without copies of
// another type (QJsonArray iterators give QJsonValueRef).
QList<QJsonValue> items(const QJsonObject &object, const QString &key)
{
    const QJsonArray array = object.value(key).toArray();
    return {array.begin(), array.end()};
}

// Every link of a page in the JSON form: facts, sections, recipe cells, results and machines.
QList<QJsonValue> linksOf(const QJsonObject &page)
{
    QList<QJsonValue> links;
    const QList<QJsonValue> facts = items(page, u"facts"_s);
    for (const QJsonValue &fact : facts) {
        links += items(fact.toObject(), u"links"_s);
    }

    const QList<QJsonValue> sections = items(page, u"sections"_s);
    for (const QJsonValue &section : sections) {
        links += items(section.toObject(), u"links"_s);
        const QList<QJsonValue> recipes = items(section.toObject(), u"recipes"_s);
        for (const QJsonValue &recipe : recipes) {
            // Empty cells of a recipe grid are null; every other link is an object.
            const QList<QJsonValue> cells = items(recipe.toObject(), u"cells"_s);
            std::ranges::copy_if(cells, std::back_inserter(links), [](const QJsonValue &cell) { return !cell.isNull(); });
            links += items(recipe.toObject(), u"machines"_s);
            links.append(recipe.toObject().value(u"result"_s));
        }
    }
    return links;
}

} // namespace

class TestWebHandbook : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content;

private slots:
    void initTestCase()
    {
        QStringList warnings;
        QcResult<std::shared_ptr<QcContentRegistry>> loaded = QcBaseContent::load(warnings);
        QVERIFY(loaded);
        m_content = *loaded;
    }

    void dataMatchesRepository_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << u"en"_s;
        QTest::newRow("de") << u"de"_s;
    }

    void dataMatchesRepository()
    {
        QFETCH(QString, language);
        const QcHandbookWeb::File file = QcHandbookWeb::data(build(*m_content, language));
        QCOMPARE(file.name, language + u".json"_s);
        QVERIFY2(readData(file.name) == file.content, qPrintable(u"%1 differs from a fresh export"_s.arg(dataFile(file.name))));
    }

    void languagesAndThemeMatchRepository()
    {
        QStringList languages = m_content->languages();
        languages.sort();
        QCOMPARE(languages, (QStringList{u"de"_s, u"en"_s}));

        const QcHandbookWeb::File list = QcHandbookWeb::languages({u"en"_s, u"de"_s});
        QCOMPARE(readData(list.name), list.content);
        const QJsonArray entries = QJsonDocument::fromJson(list.content).object().value(u"languages"_s).toArray();
        QCOMPARE(entries.at(0).toObject().value(u"name"_s).toString(), u"English"_s);
        QCOMPARE(entries.at(1).toObject().value(u"name"_s).toString(), u"Deutsch"_s);

        const QcHandbookWeb::File theme = QcWebHandbookAssets::theme();
        QCOMPARE(readData(theme.name), theme.content);
    }

    // Pixels, not PNG bytes: another Qt or zlib may compress the same sheet differently. The data
    // may come from another Qt 6.8 release than CI's 6.8.3; a difference of 2 per channel allows for
    // rounding in another patch release, a wrong or moved icon differs far more.
    void iconSheetMatchesRepository()
    {
        const QcTextureAtlas atlas = QcTextureAtlas::build(*m_content);
        const QcBlockRenderTable table(*m_content, atlas);
        const QList<QcHandbookWeb::File> files =
            QcWebHandbookAssets::iconSheet(QcItemIconSet::draw(*m_content, atlas, &table, QcWebHandbookAssets::iconSize));
        QCOMPARE(files.size(), 2);
        QCOMPARE(files[1].name, u"icons.json"_s);
        QCOMPARE(readData(u"icons.json"_s), files[1].content);

        const QImage fresh = QImage::fromData(files[0].content, "PNG").convertToFormat(QImage::Format_ARGB32);
        const QImage stored = QImage(dataFile(u"icons.png"_s)).convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!fresh.isNull());
        QCOMPARE(stored.size(), fresh.size());
        const int difference = largestDifference(stored, fresh);
        QVERIFY2(difference <= 2, qPrintable(u"web/handbook/data/icons.png differs from a fresh export by up to %1 per channel"_s.arg(difference)));
    }

    void everyShownIconIsOnTheSheet()
    {
        const QJsonObject cells = QJsonDocument::fromJson(readData(u"icons.json"_s)).object().value(u"icons"_s).toObject();
        QVERIFY(cells.size() > 300);
        for (const QString &language : {u"en"_s, u"de"_s}) {
            const QStringList shown = QcHandbookWeb::icons(build(*m_content, language));
            QVERIFY(shown.contains(u"qtcraft:furnace"_s));
            for (const QString &name : shown) {
                QVERIFY2(cells.contains(name), qPrintable(u"no icon for %1"_s.arg(name)));
            }
        }
    }

    // The page follows every link and lists every entry of a category; none may lead nowhere.
    void everyLinkAndListedEntryHasAPage()
    {
        const QJsonObject root = QJsonDocument::fromJson(readData(u"en.json"_s)).object();
        const QJsonObject entries = root.value(u"entries"_s).toObject();
        QVERIFY(entries.size() > 400);

        qsizetype listed = 0;
        const QList<QJsonValue> categories = items(root, u"categories"_s);
        for (const QJsonValue &category : categories) {
            const QList<QJsonValue> ids = items(category.toObject(), u"entries"_s);
            QVERIFY(!ids.isEmpty());
            for (const QJsonValue &id : ids) {
                QVERIFY2(entries.contains(id.toString()), qPrintable(id.toString()));
                QCOMPARE(entries.value(id.toString()).toObject().value(u"category"_s), category.toObject().value(u"id"_s));
            }
            listed += ids.size();
        }
        QCOMPARE(listed, entries.size());

        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const QJsonObject page = it.value().toObject();
            QVERIFY(!page.value(u"englishTitle"_s).toString().isEmpty());
            const QList<QJsonValue> links = linksOf(page);
            for (const QJsonValue &link : links) {
                const QString target = link.toObject().value(u"target"_s).toString();
                QVERIFY2(entries.contains(target), qPrintable(u"%1 links to %2"_s.arg(it.key(), target)));
            }
        }
    }

    // web/handbook/search.js must find what the game finds, in the same order.
    void webSearchMatchesTheGame_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << u"en"_s;
        QTest::newRow("de") << u"de"_s;
    }

    void webSearchMatchesTheGame()
    {
        QFETCH(QString, language);
        const QcHandbook book = build(*m_content, language);
        QJSEngine engine;
        const QJSValue loaded = engine.evaluate(QString::fromUtf8(readSource(u"web/handbook/search.js"_s)), u"search.js"_s);
        QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        engine.globalObject().setProperty(u"bookText"_s, QString::fromUtf8(QcHandbookWeb::data(book).content));
        const QJSValue prepared = engine.evaluate(u"var book = JSON.parse(bookText); prepareSearch(book);"_s);
        QVERIFY2(!prepared.isError(), qPrintable(prepared.toString()));
        const QJSValue search = engine.evaluate(u"(function (text) { return searchHandbook(book, text).map(entry => entry.id).join('\\n'); })"_s);

        const QStringList queries = searchQueries(book);
        QVERIFY(queries.size() > 300);
        for (const QString &query : queries) {
            QStringList expected;
            const QList<qsizetype> hits = book.search(query);
            for (const qsizetype index : hits) {
                expected.append(book.entries()[index].id);
            }
            const QJSValue found = search.call({QJSValue(query)});
            QVERIFY2(!found.isError(), qPrintable(found.toString()));
            const QStringList web = found.toString().split(u'\n', Qt::SkipEmptyParts);
            const auto [webAt, gameAt] = std::ranges::mismatch(web, expected);
            const qsizetype at = webAt - web.begin();
            QVERIFY2(web == expected, qPrintable(u"query '%1', hit %2 of %3/%4: web %5, game %6"_s.arg(query)
                                                     .arg(at)
                                                     .arg(web.size())
                                                     .arg(expected.size())
                                                     .arg(webAt != web.end() ? *webAt : u"-"_s, gameAt != expected.end() ? *gameAt : u"-"_s)));
        }
    }

    void sheetPlacesIconsByNameAndScalesThem()
    {
        const QHash<QString, QImage> icons{{u"b"_s, solid(Qt::red, 64)}, {u"a"_s, solid(Qt::blue, 48)}};
        const QList<QcHandbookWeb::File> files = QcWebHandbookAssets::iconSheet(icons);
        const QImage sheet = QImage::fromData(files[0].content, "PNG");
        QCOMPARE(sheet.size(), QSize(QcWebHandbookAssets::sheetColumns * 64, 64));

        const QJsonObject cells = QJsonDocument::fromJson(files[1].content).object().value(u"icons"_s).toObject();
        QCOMPARE(cells.value(u"a"_s).toInt(), 0);
        QCOMPARE(cells.value(u"b"_s).toInt(), 1);

        const QHash<QString, QImage> back = QcWebHandbookAssets::readSheet(sheet, files[1].content);
        QCOMPARE(back.value(u"a"_s).convertToFormat(QImage::Format_ARGB32), solid(Qt::blue, 64));
        QCOMPARE(back.value(u"b"_s).convertToFormat(QImage::Format_ARGB32), solid(Qt::red, 64));
    }

    void themeHasEveryPaletteColour()
    {
        const QByteArray css = QcWebHandbookAssets::theme().content;
        for (const QcUiPalette::Entry &entry : QcUiPalette::entries()) {
            const QByteArray variable = QByteArray("--qc-") + entry.name + ":";
            QVERIFY2(css.contains(variable), entry.name);
        }
        QVERIFY(css.contains("--qc-panel: rgba(38, 38, 38, 0.941);"));
    }
};

QTEST_GUILESS_MAIN(TestWebHandbook)
#include "test_web_handbook.moc"
