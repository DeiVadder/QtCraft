#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/parser/QcContentParser.h"
#include "QcTestUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// What content packs give the handbook: chapter files, the description keys of blocks and items,
// and language files (texts and handbook texts per language).
class TestHandbookFiles : public QObject
{
    Q_OBJECT

private slots:
    void languageFilesKeepDotsOtherThanTheHandbookSuffix()
    {
        QTemporaryDir dir;
        const auto write = [&dir](const QString &name, const QByteArray &content) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).path());
            QFile file(dir.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
        };

        QVERIFY(write(u"pack.json"_s, R"({"id":"langs","version":"1"})"));
        QVERIFY(write(u"lang/en.json"_s, R"({"a":"A"})"));
        QVERIFY(write(u"lang/de.handbook.json"_s, R"({"b":"B"})"));
        QVERIFY(write(u"lang/pt.BR.json"_s, R"({"c":"C"})"));

        QcContentRegistry content;
        QVERIFY(QcContentPackLoader::load(dir.path(), content).has_value());

        QStringList languages = content.translations().languages();
        languages.sort();
        QCOMPARE(languages, (QStringList{u"de"_s, u"en"_s, u"pt.BR"_s}));
        QCOMPARE(content.translations().translate(u"b"_s, u"de"_s), u"B"_s);
    }

    void chapterFilesAreValidated()
    {
        const QcResult<QcHandbookChapter> chapter = QcContentParser::parseHandbookChapter(
            QJsonObject{{u"name"_s, u"tips"_s}, {u"sections"_s, QJsonArray{u"one"_s, u"two"_s}}, {u"related"_s, QJsonArray{u"stone"_s}}},
            u"mod"_s);
        QVERIFY2(chapter.has_value(), qcErrorText(chapter).constData());
        QCOMPARE(chapter->name, u"mod:tips"_s);
        QCOMPARE(chapter->titleKey, u"chapter.mod.tips"_s);
        QCOMPARE(chapter->related, QStringList{u"mod:stone"_s});
        QCOMPARE(chapter->order, 100);

        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}}, u"mod"_s).has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"Bad Name"_s}}}, u"mod"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"a"_s, u"a"_s}}}, u"mod"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(
                     QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"a"_s}}, {u"related"_s, u"stone"_s}}, u"mod"_s)
                     .has_value());

        const QcResult<QcBlockDefinition> block =
            QcContentParser::parseBlock(QJsonObject{{u"name"_s, u"rock"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"rock"_s}}},
                                                    {u"description"_s, u"my.text"_s}},
                                        u"mod"_s);
        QVERIFY2(block.has_value(), qcErrorText(block).constData());
        QCOMPARE(block->descriptionKey, u"my.text"_s);

        const QcResult<QcItemDefinition> item = QcContentParser::parseItem(QJsonObject{{u"name"_s, u"gem"_s}}, u"mod"_s);
        QVERIFY2(item.has_value(), qcErrorText(item).constData());
        QCOMPARE(item->descriptionKey, u"item.mod.gem.description"_s);

        QVERIFY(!QcContentParser::parseBlock(QJsonObject{{u"name"_s, u"rock"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"rock"_s}}},
                                                         {u"description"_s, 3}},
                                             u"mod"_s)
                     .has_value());
    }
};

QTEST_GUILESS_MAIN(TestHandbookFiles)
#include "test_handbook_files.moc"
