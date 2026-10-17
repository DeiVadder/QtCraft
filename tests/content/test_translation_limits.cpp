#include "common/content/loader/QcBaseContent.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/content/QcModLoader.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

void writeJson(const QString &path, const QJsonObject &object)
{
    writeText(path, QJsonDocument(object).toJson());
}

// A language file with `count` entries "<prefix><i>": "x" (written as text: QJsonObject::insert is
// linear in the size).
void writeEntries(const QString &path, const QByteArray &prefix, qsizetype count)
{
    QByteArray text = "{";
    for (qsizetype i = 0; i < count; ++i) {
        text += (i == 0 ? "\"" : ",\"") + prefix + QByteArray::number(i) + "\":\"x\"";
    }
    text += '}';
    writeText(path, text);
}

void writeManifest(const QString &directory, const QString &id)
{
    writeJson(directory + u"/pack.json"_s, {{u"id"_s, id}, {u"version"_s, u"1"_s}});
}

const QcModStatus *find(const QList<QcModStatus> &mods, const QString &id)
{
    const auto it = std::ranges::find(mods, id, [](const QcModStatus &mod) { return mod.info.id; });
    return it == mods.end() ? nullptr : &*it;
}

// The base game's translations as the mod loader sees them while it reads a mod's files: the base
// pack is loaded but not finished, so the names of building parts are not there yet.
qsizetype baseTotalWhileReading()
{
    QcContentRegistry base;
    QStringList warnings;
    return QcBaseContent::loadInto(base, warnings) ? base.translationTotal() : 0;
}

// The base game's translations after finish(), with the names of its building parts.
qsizetype baseTotalFinished()
{
    QStringList warnings;
    const QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(warnings);
    return base ? (*base)->translationTotal() : 0;
}

} // namespace

// Bounds on translations (decision E-371): a language file holds at most what a client takes for one
// language, all languages together at most QcStreamLimits::maxTranslations, checked while a mod is
// read, after its parts are named and again by every client.
class TestTranslationLimits : public QObject
{
    Q_OBJECT

private slots:
    void totalCountsEveryLanguage()
    {
        QcContentRegistry registry;
        registry.addTranslations(u"en"_s, {{u"a"_s, u"A"_s}, {u"b"_s, u"B"_s}});
        registry.addTranslations(u"de"_s, {{u"a"_s, u"A"_s}});
        registry.addTranslations(u"en"_s, {{u"a"_s, u"A2"_s}});
        QCOMPARE(registry.translationTotal(), 3);
        QCOMPARE(registry.translationKeys(u"en"_s), (QStringList{u"a"_s, u"b"_s}));
        QCOMPARE(registry.translationKeys(u"fr"_s), QStringList());
    }

    void aFileBeyondOneLanguageIsRefusedBeforeItsTableIsBuilt()
    {
        const QJsonObject fits = QJsonDocument::fromJson("{\"a\":\"b\"}").object();
        QVERIFY(QcContentParser::parseTranslations(fits).has_value());

        QByteArray text = "{";
        for (quint32 i = 0; i <= QcStreamLimits::maxDefinitions; ++i) {
            text += (i == 0 ? "\"" : ",\"") + QByteArray::number(i) + "\":\"\"";
        }
        text += '}';
        const QcResult<QHash<QString, QString>> parsed = QcContentParser::parseTranslations(QJsonDocument::fromJson(text).object());
        QVERIFY(!parsed.has_value());
        QVERIFY2(parsed.error().contains(u"more than 65536 translations in one file"_s), qPrintable(parsed.error()));
    }

    void clientsRefuseMoreTranslationsThanTheBudget()
    {
        QcContentRegistry registry;
        const qsizetype perLanguage = QcStreamLimits::maxDefinitions;
        const qsizetype languages = QcStreamLimits::maxTranslations / perLanguage;
        for (qsizetype language = 0; language < languages; ++language) {
            QHash<QString, QString> entries;
            for (qsizetype i = 0; i < perLanguage; ++i) {
                entries.insert(QString::number(i), QString());
            }
            registry.addTranslations(u"l%1"_s.arg(language), entries);
        }

        const auto roundTrip = [](const QcContentRegistry &content) {
            QByteArray data;
            {
                QDataStream out(&data, QIODevice::WriteOnly);
                content.writeTo(out);
            }
            QDataStream in(data);
            return QcContentRegistry::readFrom(in);
        };

        // Exactly the budget is fine, one more entry is not.
        QCOMPARE(registry.translationTotal(), qsizetype(QcStreamLimits::maxTranslations));
        QVERIFY(roundTrip(registry).has_value());
        registry.addTranslations(u"extra"_s, {{u"k"_s, u"v"_s}});
        const QcResult<QcContentRegistry> refused = roundTrip(registry);
        QVERIFY(!refused.has_value());
        QVERIFY2(refused.error().contains(u"translations"_s), qPrintable(refused.error()));
    }

    // Files that would exceed the budget are skipped with a warning while the mod is read, so the table
    // never grows past it; the rest of the mod loads.
    void filesBeyondTheBudgetAreSkipped()
    {
        QTemporaryDir dir;
        const QString mod = dir.filePath(u"many"_s);
        writeManifest(mod, u"many"_s);
        const qsizetype perFile = 60000;
        const qsizetype files = QcStreamLimits::maxTranslations / perFile + 1;
        for (qsizetype file = 0; file < files; ++file) {
            writeEntries(mod + u"/lang/l%1.json"_s.arg(file), "k", perFile);
        }
        // Overriding keys that exist adds nothing and always fits.
        writeJson(mod + u"/lang/en.json"_s, {{u"block.qtcraft.stone"_s, u"Rock"_s}});

        QStringList warnings;
        const QcResult<QcModLoader::Result> loaded = QcModLoader::load(dir.path(), warnings);
        QVERIFY(loaded.has_value());
        const QcModStatus *status = find(loaded->mods, u"many"_s);
        QVERIFY(status);
        QCOMPARE(status->state, QcModStatus::State::Loaded);
        QVERIFY(loaded->content->translationTotal() <= qsizetype(QcStreamLimits::maxTranslations));
        QCOMPARE(loaded->content->translate(u"block.qtcraft.stone"_s, u"en"_s), u"Rock"_s);

        // The files are checked against the unfinished content (without part names).
        const qsizetype fitting = (QcStreamLimits::maxTranslations - baseTotalWhileReading()) / perFile;
        QCOMPARE(loaded->content->translationTotal(), baseTotalFinished() + fitting * perFile);
        QCOMPARE(loaded->content->languages().size(), 2 + fitting);
        const qsizetype skipped = std::ranges::count_if(status->warnings, [](const QString &warning) {
            return warning.contains(u"more than 262144 translations in all languages"_s);
        });
        QCOMPARE(skipped, files - fitting);
    }

    // Names of building parts are added when the content is finished, after the files were read; a
    // mod whose parts push the total over the budget is refused like any content clients reject.
    void partNamesCountTowardsTheBudget()
    {
        QTemporaryDir dir;
        const QString mod = dir.filePath(u"pillars"_s);
        writeManifest(mod, u"pillars"_s);
        writeJson(mod + u"/parts/pillar.json"_s,
                  {{u"name"_s, u"pillar"_s},
                   {u"kinds"_s, QJsonArray{u"stone"_s}},
                   {u"block"_s, QJsonObject{{u"shape"_s, u"slab"_s}}},
                   {u"recipe"_s, QJsonObject{{u"pattern"_s, QJsonArray{u"#"_s, u"#"_s}}, {u"key"_s, QJsonObject{{u"#"_s, u"@"_s}}}, {u"count"_s, 2}}},
                   {u"translations"_s, QJsonObject{{u"en"_s, u"%1 pillar"_s}, {u"de"_s, u"%1säule"_s}}}});
        // The files fit while they are read (the base game's part names come later); with the base
        // game's own part names the total reaches the budget exactly, the new family's names (two per
        // stone material) go beyond it, so the whole mod is refused.
        QVERIFY(baseTotalFinished() > baseTotalWhileReading());
        qsizetype room = QcStreamLimits::maxTranslations - baseTotalFinished();
        for (int file = 0; room > 0; ++file) {
            const qsizetype count = std::min<qsizetype>(room, 60000);
            writeEntries(mod + u"/lang/l%1.json"_s.arg(file), "k", count);
            room -= count;
        }

        QStringList warnings;
        const QcResult<QcModLoader::Result> loaded = QcModLoader::load(dir.path(), warnings);
        QVERIFY(loaded.has_value());
        const QcModStatus *status = find(loaded->mods, u"pillars"_s);
        QVERIFY(status);
        QCOMPARE(status->state, QcModStatus::State::Failed);
        QVERIFY2(status->reason.contains(u"more than 262144 translations in all languages"_s), qPrintable(status->reason));
        QCOMPARE(loaded->content->translationTotal(), baseTotalFinished());
    }
};

QTEST_GUILESS_MAIN(TestTranslationLimits)
#include "test_translation_limits.moc"
