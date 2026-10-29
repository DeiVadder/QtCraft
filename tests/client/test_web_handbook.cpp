#include "clientcore/icons/QcItemIconSet.h"
#include "clientcore/icons/QcWebHandbookAssets.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/settings/QcUiPalette.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/gameplay/QcKeyBindings.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "common/handbook/QcHandbookWeb.h"
#include "QcTestUtil.h"
#include "worlds/QcHandbookRules.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

QcHandbook build(const QcContentRegistry &content, const QString &language)
{
    return QcHandbookBuilder::build(content, language, QcHandbookRules::values(content),
                                    QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
}

QByteArray readSource(const QString &path)
{
    QFile file(QDir(QStringLiteral(QTCRAFT_SOURCE_DIR)).filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
    // What qtcraft-headless --export-handbook-web wrote in initTestCase, in <m_export>/data.
    // web/handbook/data is not in git: the Pages workflow writes it the same way before it
    // publishes web/.
    QTemporaryDir m_export;

    QString dataFile(const QString &name) const { return m_export.filePath(u"data/"_s + name); }

    QByteArray readData(const QString &name) const
    {
        QFile file(dataFile(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    // Runs the exporter as the Pages workflow does; every test below reads what it wrote.
    void initTestCase()
    {
        QcResult<QcBaseContent::Loaded> loaded = QcTestUtil::baseContent();
        QVERIFY(loaded);
        m_content = loaded->registry;

        QVERIFY(m_export.isValid());
        QProcess exporter;
        exporter.setProgram(QStringLiteral(QTCRAFT_HEADLESS_PATH));
        exporter.setArguments({u"--no-mods"_s, u"--export-handbook-web"_s, dataFile({})});
        exporter.setProcessChannelMode(QProcess::MergedChannels);
        exporter.start();
        static constexpr int exportTimeoutMs = 240'000;
        QVERIFY2(exporter.waitForFinished(exportTimeoutMs), "the exporter did not finish");
        const QByteArray output = exporter.readAll();
        QVERIFY2(exporter.exitStatus() == QProcess::NormalExit && exporter.exitCode() == 0, output.constData());
    }

    void exportWritesEveryFile()
    {
        QStringList written = QDir(dataFile({})).entryList(QDir::Files);
        written.sort();
        QCOMPARE(written, (QStringList{u"de.json"_s, u"en.json"_s, u"icons.json"_s, u"icons.png"_s, u"languages.json"_s, u"theme.css"_s}));
    }

    // The keys web/handbook/handbook.js reads from each file.
    void exportedDataHasTheKeysThePageReads()
    {
        const QJsonObject languages = QJsonDocument::fromJson(readData(u"languages.json"_s)).object();
        const QList<QJsonValue> list = items(languages, u"languages"_s);
        QCOMPARE(list.size(), 2);
        for (const QJsonValue &language : list) {
            QVERIFY(language.toObject().contains(u"id"_s) && language.toObject().contains(u"name"_s));
        }

        const QJsonObject icons = QJsonDocument::fromJson(readData(u"icons.json"_s)).object();
        QVERIFY(icons.value(u"columns"_s).toInt() > 0);
        QVERIFY(icons.value(u"icons"_s).isObject());
        QVERIFY(icons.value(u"size"_s).toInt() > 0);

        const QJsonObject book = QJsonDocument::fromJson(readData(u"en.json"_s)).object();
        QVERIFY(!items(book, u"packs"_s).isEmpty());
        const QList<QJsonValue> categories = items(book, u"categories"_s);
        QVERIFY(!categories.isEmpty());
        for (const QJsonValue &category : categories) {
            for (const QString &key : {u"id"_s, u"title"_s, u"entries"_s}) {
                QVERIFY2(category.toObject().contains(key), qPrintable(key));
            }
        }
        const QJsonObject entries = book.value(u"entries"_s).toObject();
        QVERIFY(!entries.isEmpty());
        static const QStringList entryKeys{u"title"_s,    u"category"_s, u"categoryTitle"_s, u"pack"_s, u"packName"_s,
                                           u"facts"_s,    u"sections"_s, u"text"_s,          u"icon"_s};
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            for (const QString &key : entryKeys) {
                QVERIFY2(it.value().toObject().contains(key), qPrintable(u"%1 has no %2"_s.arg(it.key(), key)));
            }
        }
    }

    void exportedDataMatchesTheHandbook_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << u"en"_s;
        QTest::newRow("de") << u"de"_s;
    }

    void exportedDataMatchesTheHandbook()
    {
        QFETCH(QString, language);
        const QcHandbookWeb::File file = QcHandbookWeb::data(build(*m_content, language));
        QCOMPARE(file.name, language + u".json"_s);
        QVERIFY2(readData(file.name) == file.content, qPrintable(u"%1 differs from the handbook built here"_s.arg(file.name)));
    }

    void exportedLanguagesAndThemeAreComplete()
    {
        QStringList languages = m_content->translations().languages();
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

    // Pixels, not PNG bytes: the same sheet may be compressed differently. The exporter and this
    // test draw with the same build, so every pixel matches.
    void exportedIconSheetMatchesTheIcons()
    {
        const QcTextureAtlas atlas = QcTextureAtlas::build(*m_content);
        const QcBlockRenderTable table(*m_content, atlas);
        const QList<QcHandbookWeb::File> files =
            QcWebHandbookAssets::iconSheet(QcItemIconSet::draw(*m_content, atlas, &table, QcWebHandbookAssets::iconSize));
        QCOMPARE(files.size(), 2);
        QCOMPARE(files[1].name, u"icons.json"_s);
        QCOMPARE(readData(u"icons.json"_s), files[1].content);

        const QImage fresh = QImage::fromData(files[0].content, "PNG").convertToFormat(QImage::Format_ARGB32);
        const QImage exported = QImage(dataFile(u"icons.png"_s)).convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!fresh.isNull());
        QCOMPARE(exported.size(), fresh.size());
        QVERIFY2(exported == fresh, "the exported icon sheet differs from the icons drawn here");
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
