#include "QcTestUtil.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestWorldLibrary : public QObject
{
    Q_OBJECT

private slots:
    void createListAndRemove()
    {
        QTemporaryDir root;
        const QcWorldLibrary library(root.path());

        const auto first = library.create(u"My World!"_s, 5);
        QVERIFY2(first.has_value(), qcErrorText(first).constData());
        QCOMPARE(first->folder, u"My_World_"_s);
        const auto second = library.create(u"My World!"_s, 6);
        QCOMPARE(second->folder, u"My_World__2"_s);

        const QList<QcWorldLibrary::Entry> entries = library.list();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).info.name, u"My World!"_s);

        QVERIFY(library.remove(first->folder).has_value());
        QCOMPARE(library.list().size(), 1);
        QVERIFY(!QDir(first->directory).exists());
    }

    void worldOfAnOlderVersionIsListedAsTooOld()
    {
        // The world in the format before stage 31.1 (decision E-356) and a current one.
        QTemporaryDir root;
        const QDir old(QStringLiteral(QTCRAFT_OLD_WORLD));
        QVERIFY(QDir().mkpath(root.filePath(u"Old/regions"_s)));
        const QStringList files = old.entryList(QDir::Files);
        for (const QString &file : files) {
            QVERIFY(QFile::copy(old.filePath(file), root.filePath(u"Old/"_s + file)));
        }
        const QcWorldLibrary library(root.path());
        QVERIFY(library.create(u"New"_s, 1).has_value());

        const QList<QcWorldLibrary::Entry> entries = library.list();
        QCOMPARE(entries.size(), 2);
        const auto oldEntry = std::ranges::find(entries, u"Old"_s, &QcWorldLibrary::Entry::folder);
        const auto newEntry = std::ranges::find(entries, u"New"_s, &QcWorldLibrary::Entry::folder);
        QVERIFY(oldEntry != entries.end() && newEntry != entries.end());
        QVERIFY(oldEntry->tooOld);
        QVERIFY(!oldEntry->error.isEmpty());
        QVERIFY(!newEntry->tooOld);
        QVERIFY(newEntry->error.isEmpty());
    }

    void refusesToDeleteOutsideTheLibrary_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::newRow("parent") << u".."_s;
        QTest::newRow("path") << u"../other"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("not a world") << u"plain"_s;
    }

    void refusesToDeleteOutsideTheLibrary()
    {
        QFETCH(QString, folder);

        // The library lives in base/worlds; base and base/other look like worlds too, so only the
        // folder-name check can protect them.
        QTemporaryDir base;
        const QString root = QDir(base.path()).filePath(u"worlds"_s);
        QVERIFY(QDir().mkpath(QDir(root).filePath(u"plain"_s)));
        QVERIFY(QDir().mkpath(QDir(base.path()).filePath(u"other"_s)));
        for (const QString &file : {QDir(base.path()).filePath(u"world.json"_s), QDir(base.path()).filePath(u"other/world.json"_s)}) {
            QFile json(file);
            QVERIFY(json.open(QIODevice::WriteOnly));
            json.write("{}");
        }

        const QcWorldLibrary library(root);
        QVERIFY(!library.remove(folder).has_value());
        QVERIFY(QFile::exists(QDir(base.path()).filePath(u"world.json"_s)));
        QVERIFY(QFile::exists(QDir(base.path()).filePath(u"other/world.json"_s)));
        QVERIFY(QDir(QDir(root).filePath(u"plain"_s)).exists());
    }

    void refusesToFollowLinks()
    {
        QTemporaryDir base;
        const QString root = QDir(base.path()).filePath(u"worlds"_s);
        const QString target = QDir(base.path()).filePath(u"precious"_s);
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(target));

        QFile json(QDir(target).filePath(u"world.json"_s));
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.close();
        QVERIFY(QFile::link(target, QDir(root).filePath(u"linked"_s)));

        QVERIFY(!QcWorldLibrary(root).remove(u"linked"_s).has_value());
        QVERIFY(QFile::exists(QDir(target).filePath(u"world.json"_s)));
    }

    void damagedWorldsAreListedWithAnError()
    {
        QTemporaryDir root;
        const QcWorldLibrary library(root.path());
        const auto entry = library.create(u"Broken"_s, 1);

        QFile info(QDir(entry->directory).filePath(u"world.json"_s));
        QVERIFY(info.open(QIODevice::WriteOnly | QIODevice::Truncate));
        info.write("not json");
        info.close();

        const QList<QcWorldLibrary::Entry> entries = library.list();
        QCOMPARE(entries.size(), 1);
        QVERIFY(!entries.at(0).error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWorldLibrary)
#include "test_world_library.moc"
