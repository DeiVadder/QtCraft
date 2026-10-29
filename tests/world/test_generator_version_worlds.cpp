// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/network/QcServerSettings.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// A world keeps the world generator version it was created with (decision E-922): world.json names
// it, worlds without it are of the first version, the server opens a world only with its version, and
// a version the game does not have is refused with the reason.
namespace {

const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeJson(const QString &path, const QJsonObject &json)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(json).toJson()) > 0;
}

// world.json of `directory` with the generator's "version" set to `version` (removed when undefined).
bool setStoredVersion(const QString &directory, const QJsonValue &version)
{
    const QString path = QDir(directory).filePath(u"world.json"_s);
    QJsonObject json = readJson(path);
    QJsonObject generator = json.value(u"generator"_s).toObject();
    if (version.isUndefined()) {
        generator.remove(u"version"_s);
    } else {
        generator.insert(u"version"_s, version);
    }
    json.insert(u"generator"_s, generator);
    return writeJson(path, json);
}

} // namespace

class TestGeneratorVersionWorlds : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // Opens the world of `directory` on a server for generator version `version`, with a generator of
    // `generates` (the configured version when not given; a version the game does not have yet is
    // given to a current generator, as a later game would make it).
    QcStatus open(const QString &directory, qint64 seed, int version, std::optional<int> generates = std::nullopt,
                  const QString &type = terrain) const
    {
        QcServerConfig config;
        config.worldDirectory = directory;
        config.seed = seed;
        config.generator = type;
        config.generatorVersion = version;
        std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(type, m_content, seed, {}, QcGeneratorVersion::current);
        generator->setVersion(generates.value_or(version));
        QcServer server(config, m_content, std::move(generator));
        const QcStatus opened = server.openWorld();
        if (!opened) {
            return opened;
        }
        return server.stop();
    }

private slots:
    void newWorldsKeepTheCurrentVersion()
    {
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QcResult<QcWorldLibrary::Entry> created = library.create(u"Versioned"_s, 5);
        QVERIFY(created);
        const QJsonObject json = readJson(QDir(created->directory).filePath(u"world.json"_s));
        QCOMPARE(json.value(u"formatVersion"_s).toInt(), QcWorldInfo::formatVersion);
        QCOMPARE(json.value(u"generator"_s).toObject().value(u"version"_s).toInt(), QcGeneratorVersion::current);
        QCOMPARE(library.list().constFirst().info.generatorVersion, QcGeneratorVersion::current);

        QVERIFY(open(created->directory, 5, QcGeneratorVersion::current));
        // The server wrote it back unchanged.
        QCOMPARE(readJson(QDir(created->directory).filePath(u"world.json"_s)).value(u"generator"_s).toObject().value(u"version"_s).toInt(),
                 QcGeneratorVersion::current);

        // Opened for another version, the world refuses and stays as it was.
        const QString path = QDir(created->directory).filePath(u"world.json"_s);
        const QByteArray before = fileBytes(path);
        const QcStatus refused = open(created->directory, 5, QcGeneratorVersion::current + 1);
        QVERIFY(!refused);
        QVERIFY2(qcErrorText(refused).contains("made by world generator version 1, not 2"), qcErrorText(refused).constData());
        QCOMPARE(fileBytes(path), before);

        // A server whose generator generates as another version than the world's refuses too.
        const QcStatus mismatched = open(created->directory, 5, QcGeneratorVersion::current, QcGeneratorVersion::current + 1);
        QVERIFY(!mismatched);
        QVERIFY2(qcErrorText(mismatched).contains("generates as version"), qcErrorText(mismatched).constData());
        QCOMPARE(fileBytes(path), before);
    }

    // A showcase world stores no version and is always of the current one (decision E-922), so it
    // opens whatever versions the game has.
    void showcaseWorldsAreNotVersioned()
    {
        QTemporaryDir directory;
        const QString showcase = QString::fromLatin1(QcWorldGenerators::showcase);
        const QcResult<QcWorldLibrary::Entry> created =
            QcWorldLibrary(directory.path()).create(u"Show"_s, 4, {.generator = showcase, .options = {}, .worldOptions = {}, .generatorVersion = 7});
        QVERIFY(created);
        QVERIFY(!readJson(QDir(created->directory).filePath(u"world.json"_s)).value(u"generator"_s).toObject().contains(u"version"_s));
        QVERIFY(setStoredVersion(created->directory, QcGeneratorVersion::maxStored + 5));
        const QcResult<QcWorldInfo> info = QcWorldStorage(created->directory, nullptr).readInfo();
        QVERIFY2(info, qcErrorText(info).constData());
        QCOMPARE(info->generatorVersion, QcGeneratorVersion::current);
        QVERIFY(open(created->directory, 4, QcGeneratorVersion::oldest, QcGeneratorVersion::current, showcase));
    }

    // A world a server creates keeps the server's version.
    void theServerWritesItsVersionIntoANewWorld()
    {
        QTemporaryDir directory;
        QVERIFY(open(directory.path(), 8, QcGeneratorVersion::current));
        const QcResult<QcWorldInfo> info = QcWorldStorage(directory.path(), nullptr).readInfo();
        QVERIFY2(info, qcErrorText(info).constData());
        QCOMPARE(info->generatorVersion, QcGeneratorVersion::current);
    }

    // Worlds saved before decision E-922 (format 18, no version) are of the first version and open.
    void worldsWithoutAVersionAreOfTheFirst()
    {
        QTemporaryDir directory;
        const QcResult<QcWorldLibrary::Entry> created = QcWorldLibrary(directory.path()).create(u"Old"_s, 9);
        QVERIFY(created);
        QVERIFY(setStoredVersion(created->directory, QJsonValue::Undefined));
        const QString path = QDir(created->directory).filePath(u"world.json"_s);
        QJsonObject json = readJson(path);
        json.insert(u"formatVersion"_s, 18);
        QVERIFY(writeJson(path, json));

        const QcResult<QcWorldInfo> info = QcWorldStorage(created->directory, nullptr).readInfo();
        QVERIFY2(info, qcErrorText(info).constData());
        QCOMPARE(info->generatorVersion, QcGeneratorVersion::unversioned);
        QVERIFY(open(created->directory, 9, QcGeneratorVersion::unversioned));
    }

    // A newer game's version is refused with the reason; the world is listed but not playable.
    void otherVersionsAreRefused_data()
    {
        QTest::addColumn<QJsonValue>("version");
        QTest::addColumn<QString>("reason");
        QTest::newRow("newer") << QJsonValue(QcGeneratorVersion::current + 1) << u"newer game with world generator version"_s;
        QTest::newRow("zero") << QJsonValue(0) << u"damaged"_s;
        QTest::newRow("fraction") << QJsonValue(1.5) << u"damaged"_s;
        QTest::newRow("text") << QJsonValue(u"1"_s) << u"damaged"_s;
        QTest::newRow("huge") << QJsonValue(QcGeneratorVersion::maxStored + 1) << u"damaged"_s;
    }

    void otherVersionsAreRefused()
    {
        QFETCH(QJsonValue, version);
        QFETCH(QString, reason);
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QcResult<QcWorldLibrary::Entry> created = library.create(u"Future"_s, 3);
        QVERIFY(created);
        QVERIFY(setStoredVersion(created->directory, version));
        const QString path = QDir(created->directory).filePath(u"world.json"_s);
        const QByteArray before = fileBytes(path);

        const QcResult<QcWorldInfo> info = QcWorldStorage(created->directory, nullptr).readInfo();
        QVERIFY(!info);
        QVERIFY2(info.error().contains(reason), qPrintable(info.error()));
        const QList<QcWorldLibrary::Entry> listed = library.list();
        QCOMPARE(listed.size(), 1);
        QVERIFY(listed.first().error.contains(reason));
        QVERIFY(!open(created->directory, 3, QcGeneratorVersion::current));
        // The refused world is left exactly as it was.
        QCOMPARE(fileBytes(path), before);
    }

    // server.json: 0 (the default) is the current version for a new world, others must be known.
    void serverSettingsNameTheVersionOfNewWorlds()
    {
        const QcResult<QcServerSettings> defaults = QcServerSettings::fromJson({});
        QVERIFY(defaults);
        QCOMPARE(defaults->generatorVersion, 0);
        QCOMPARE(defaults->newWorldGeneratorVersion(), QcGeneratorVersion::current);
        QCOMPARE(defaults->toJson().value(u"generatorVersion"_s).toInt(), 0);

        const QcResult<QcServerSettings> chosen = QcServerSettings::fromJson({{u"generatorVersion"_s, QcGeneratorVersion::oldest}});
        QVERIFY(chosen);
        QCOMPARE(chosen->newWorldGeneratorVersion(), QcGeneratorVersion::oldest);

        const QcResult<QcServerSettings> unknown = QcServerSettings::fromJson({{u"generatorVersion"_s, QcGeneratorVersion::current + 1}});
        QVERIFY(!unknown);
        QVERIFY2(unknown.error().contains(u"generatorVersion"_s), qPrintable(unknown.error()));
    }
};

QTEST_GUILESS_MAIN(TestGeneratorVersionWorlds)
#include "test_generator_version_worlds.moc"
