#include "common/content/loader/QcBaseContent.h"
#include "common/content/parser/QcContentParser.h"
#include "common/core/QcProcessMemory.h"
#include "QcTestUtil.h"
#include "worlds/QcModLoader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_DARWIN)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace Qt::StringLiterals;

// Headless measurement of the translations (stage 43): how many entries the base game holds per
// language, how many of them are names of building parts, how much heap the tables take and how long
// reading the language files takes; and the worst case a mod can cause within the loader's limits.
// Small in every test run; QTCRAFT_TRANSLATION_MEASURE=full measures the full size and prints MEASURE
// lines (Release build). Run the mod case alone (`test_translation_measure modAtTheLimits`): the peak
// memory of the process only grows.
class TestTranslationMeasure : public QObject
{
    Q_OBJECT

private:
    static bool full() { return qEnvironmentVariable("QTCRAFT_TRANSLATION_MEASURE") == u"full"_s; }

    // Heap bytes in use; 0 where the platform does not report it.
    static double heapMb()
    {
#if defined(Q_OS_DARWIN)
        malloc_statistics_t stats{};
        malloc_zone_statistics(nullptr, &stats);
        return double(stats.size_in_use) / (1024.0 * 1024.0);
#elif defined(__GLIBC__)
        return double(mallinfo2().uordblks) / (1024.0 * 1024.0);
#else
        return 0.0;
#endif
    }

    static void report(const char *what, double value, const char *unit) { qInfo("MEASURE %s %.3f %s", what, value, unit); }

    // A deep copy of every table, so its heap can be measured apart from the registry's.
    static QList<QHash<QString, QString>> copyTables(const QcContentRegistry &content)
    {
        QList<QHash<QString, QString>> tables;
        const QStringList languages = content.translations().languages();
        for (const QString &language : languages) {
            QHash<QString, QString> table;
            const QStringList keys = content.translations().keys(language);
            for (const QString &key : keys) {
                const QString text = content.translations().translate(key, language);
                table.insert(QString(key.constData(), key.size()), QString(text.constData(), text.size()));
            }
            tables.append(table);
        }
        return tables;
    }

    static void writeJson(const QString &path, const QJsonObject &json)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    }

    // A language file with `count` entries "<i in base 36>": "" (written as text: QJsonObject::insert
    // is linear in the size).
    static void writeShortEntries(const QString &path, int count)
    {
        QByteArray text = "{";
        for (int i = 0; i < count; ++i) {
            text += (i == 0 ? "\"" : ",\"") + QByteArray::number(i, 36) + "\":\"\"";
        }
        text += '}';
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    static void loadAndReport(const QString &modsDirectory)
    {
        const double peakBefore = QcProcessMemory::peakMb();
        const double heapBefore = heapMb();
        QElapsedTimer timer;
        timer.start();
        const QcModLoader::Loaded loaded = QcModLoader::load(modsDirectory);
        report("mod load", double(timer.nsecsElapsed()) / 1e6, "ms");
        QVERIFY(loaded.has_value());
        for (const QcModStatus &mod : loaded->mods) {
            qInfo("MEASURE mod %s: %s", qPrintable(mod.info.id), mod.reason.isEmpty() ? "loaded" : qPrintable(mod.reason));
        }

        const qsizetype entries = loaded->content->translations().total();
        qInfo("MEASURE translations after loading %lld entries in %lld languages", qlonglong(entries),
              qlonglong(loaded->content->translations().languages().size()));
        report("heap after loading", heapMb() - heapBefore, "MB");
        report("peak before", peakBefore, "MB");
        report("peak after", QcProcessMemory::peakMb(), "MB");
    }

private slots:
    void baseGame()
    {
        const double heapBefore = heapMb();
        QElapsedTimer timer;
        timer.start();
        const QcResult<QcBaseContent::Loaded> base = QcTestUtil::baseContent();
        const double loadMs = double(timer.nsecsElapsed()) / 1e6;
        QVERIFY(base.has_value());
        const QcContentRegistry &content = *base->registry;

        qsizetype parts = 0;
        for (const QcBlockDefinition &block : content.blocks()) {
            parts += block.partOf.isEmpty() ? 0 : 1;
        }
        const QStringList languages = content.translations().languages();
        for (const QString &language : languages) {
            qInfo("MEASURE translations %s %lld entries", qPrintable(language), qlonglong(content.translations().count(language)));
            QVERIFY(content.translations().count(language) > parts);
        }
        qInfo("MEASURE part blocks %lld", qlonglong(parts));
        report("content load", loadMs, "ms");
        report("content heap", heapMb() - heapBefore, "MB");

        const double tablesBefore = heapMb();
        const QList<QHash<QString, QString>> tables = copyTables(content);
        report("translation tables heap", heapMb() - tablesBefore, "MB");
        QCOMPARE(tables.size(), content.translations().languages().size());
    }

    // Reading and parsing the base game's language files alone.
    void languageFiles()
    {
        const QStringList files = QDir(u":/content/qtcraft/lang"_s).entryList({u"*.json"_s}, QDir::Files);
        QVERIFY(!files.isEmpty());
        const int rounds = full() ? 20 : 1;
        QElapsedTimer timer;
        timer.start();
        qsizetype entries = 0;
        for (int round = 0; round < rounds; ++round) {
            entries = 0;
            for (const QString &name : files) {
                QFile file(u":/content/qtcraft/lang/"_s + name);
                QVERIFY(file.open(QIODevice::ReadOnly));
                const QcResult<QHash<QString, QString>> parsed =
                    QcContentParser::parseTranslations(QJsonDocument::fromJson(file.readAll()).object());
                QVERIFY(parsed.has_value());
                entries += parsed->size();
            }
        }
        report("language files read and parse", double(timer.nsecsElapsed()) / 1e6 / rounds, "ms");
        qInfo("MEASURE language files %lld entries", qlonglong(entries));
    }

    // Reading one language file of almost 4 MiB with the shortest entries (the most a mod file may
    // hold): what parsing alone costs before any check can look at the entries. Run alone.
    void largeLanguageFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"l.json"_s);
        writeShortEntries(path, full() ? 380000 : 2000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        const double peakBefore = QcProcessMemory::peakMb();
        QElapsedTimer timer;
        timer.start();
        const QJsonObject json = QJsonDocument::fromJson(data).object();
        report("parse", double(timer.nsecsElapsed()) / 1e6, "ms");
        report("peak before parse", peakBefore, "MB");
        report("peak after parse", QcProcessMemory::peakMb(), "MB");
        QVERIFY(!json.isEmpty());
    }

    // The worst mod before the translation budget (decision E-371): as many short translations as the
    // 32 MiB content sync allows (34 languages x 60000 entries of 16 bytes each in the sync, about 2
    // million). With the budget only the files within 262144 entries in all languages load (4 of 34),
    // which is close to the most a mod can add now; the same mod is measured before and after.
    void modAtTheLimits()
    {
        QTemporaryDir dir;
        // 16 bytes per entry in the content sync (key of 3-4 characters, empty text): 34 x 60000 fit
        // the 32 MiB.
        const int languages = full() ? 34 : 2;
        const int perLanguage = full() ? 60000 : 2000;
        writeJson(dir.filePath(u"many/pack.json"_s), {{u"id"_s, u"many"_s}, {u"version"_s, u"1"_s}});
        for (int language = 0; language < languages; ++language) {
            writeShortEntries(dir.filePath(u"many/lang/l%1.json"_s.arg(language)), perLanguage);
        }

        loadAndReport(dir.path());
    }

    // Two mods whose language files almost fill what mods may read (each at most 32 MiB, together
    // 64 MiB): 8 files of almost 4 MiB each with the shortest entries. Before the budget (decision
    // E-371) each mod was loaded in full and refused afterwards; now every file is refused by the parser
    // (more than one language takes) before its table is built, and what remains is the cost of parsing
    // the files. Run alone, like modAtTheLimits.
    void modBeyondTheLimits()
    {
        QTemporaryDir dir;
        const int files = full() ? 8 : 1;
        // At most 10 bytes per entry in the file ("abcd":"",), just under 4 MiB per file.
        const int perFile = full() ? 380000 : 2000;
        for (const QString &id : {u"huge1"_s, u"huge2"_s}) {
            writeJson(dir.filePath(id + u"/pack.json"_s), {{u"id"_s, id}, {u"version"_s, u"1"_s}});
            for (int file = 0; file < files; ++file) {
                writeShortEntries(dir.filePath(id + u"/lang/l%1.json"_s.arg(file)), perFile);
            }
        }
        loadAndReport(dir.path());
    }
};

QTEST_GUILESS_MAIN(TestTranslationMeasure)
#include "test_translation_measure.moc"
