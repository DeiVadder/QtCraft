#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/gameplay/QcCrafting.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcInProcessTransport.h"
#include "common/protocol/QcStreamLimits.h"
#include "QcRawClient.h"
#include "QcTestMachines.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/blocks/QcBlockRules.h"
#include "server/blocks/QcFallingBlockManager.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/core/QcServer.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worlds/QcModLoader.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

QcContentPackInfo pack(const QString &id, const QStringList &dependencies = {}, const QString &version = u"1.0.0"_s)
{
    QcContentPackInfo info;
    info.id = id;
    info.name = id;
    info.version = version;
    info.dependencies = dependencies;
    info.directory = u"/mods/"_s + id;
    return info;
}

QStringList loadedIds(const QList<QcModStatus> &mods)
{
    QStringList ids;
    for (const QcModStatus &mod : mods) {
        if (mod.state == QcModStatus::State::Loaded) {
            ids.append(mod.info.id);
        }
    }
    return ids;
}

const QcModStatus *find(const QList<QcModStatus> &mods, const QString &id)
{
    const auto it = std::ranges::find(mods, id, [](const QcModStatus &mod) { return mod.info.id; });
    return it == mods.end() ? nullptr : &*it;
}

void writeJson(const QString &path, const QJsonObject &object)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson());
}

void writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// Copies the example mod shipped in the repository into `target`.
void copyExampleMod(const QString &target)
{
    const QDir source(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR "/rubies"));
    const QStringList folders = source.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QDir().mkpath(target);
    QVERIFY(QFile::copy(source.filePath(u"pack.json"_s), QDir(target).filePath(u"pack.json"_s)));

    for (const QString &folder : folders) {
        QDir().mkpath(QDir(target).filePath(folder));
        const QStringList files = QDir(source.filePath(folder)).entryList(QDir::Files);
        for (const QString &file : files) {
            QVERIFY(QFile::copy(source.filePath(folder + u'/' + file), QDir(target).filePath(folder + u'/' + file)));
        }
    }
}

const QList<QcPackReference> base{{u"qtcraft"_s, u"QtCraft"_s, u"0.1.0"_s}};

} // namespace

class TestMods : public QObject
{
    Q_OBJECT

    // The cells of each of `blocks` between y = 1 and 40 in 12 x 12 chunks generated from chunk (0, 0).
    static QHash<QcBlockId, int> deepCells(const QcTerrainGenerator &generator, const QList<QcBlockId> &blocks)
    {
        QHash<QcBlockId, int> cells;
        for (int cz = 0; cz < 12; ++cz) {
            for (int cx = 0; cx < 12; ++cx) {
                const auto chunk = generator.generate({cx, cz});
                for (int y = 1; y <= 40; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            const QcBlockId block = chunk->block(x, y, z);
                            if (blocks.contains(block)) {
                                ++cells[block];
                            }
                        }
                    }
                }
            }
        }
        return cells;
    }

private slots:
    void dependenciesLoadFirstAndTiesGoById()
    {
        const QList<QcModStatus> mods = QcModLoader::resolve(
            {pack(u"c"_s, {u"b"_s}), pack(u"d"_s), pack(u"b"_s, {u"a"_s, u"qtcraft"_s}), pack(u"a"_s)}, {}, base);
        QCOMPARE(loadedIds(mods), (QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
        const QList<QcModStatus> reversed = QcModLoader::resolve(
            {pack(u"a"_s, {u"z"_s}), pack(u"z"_s)}, {}, base);
        QCOMPARE(loadedIds(reversed), (QStringList{u"z"_s, u"a"_s}));
    }

    void brokenDependencyChainsAreSkipped()
    {
        QcContentPackInfo needsNew = pack(u"needs_new"_s, {u"qtcraft"_s});
        needsNew.minimumVersions.insert(u"qtcraft"_s, QVersionNumber(2));
        const QList<QcModStatus> mods = QcModLoader::resolve(
            {pack(u"lonely"_s, {u"missing"_s}), pack(u"child"_s, {u"lonely"_s}), pack(u"x"_s, {u"y"_s}),
             pack(u"y"_s, {u"x"_s}), pack(u"qtcraft"_s), pack(u"off"_s), pack(u"uses_off"_s, {u"off"_s}), needsNew,
             pack(u"fine"_s, {u"qtcraft"_s})},
            {u"off"_s}, base);

        QCOMPARE(loadedIds(mods), QStringList{u"fine"_s});
        QVERIFY(find(mods, u"lonely"_s)->reason.contains(u"missing dependency 'missing'"_s));
        QVERIFY(find(mods, u"child"_s)->reason.contains(u"lonely"_s));
        QVERIFY(find(mods, u"x"_s)->reason.contains(u"cycle"_s));
        QVERIFY(find(mods, u"y"_s)->reason.contains(u"cycle"_s));
        QVERIFY(find(mods, u"qtcraft"_s)->reason.contains(u"base game"_s));
        QCOMPARE(find(mods, u"off"_s)->state, QcModStatus::State::Disabled);
        QVERIFY(find(mods, u"uses_off"_s)->reason.contains(u"disabled"_s));
        QVERIFY(find(mods, u"needs_new"_s)->reason.contains(u"2 or newer"_s));
        QCOMPARE(mods.size(), 9);
    }

    void duplicateIdsKeepTheFirst()
    {
        QcContentPackInfo second = pack(u"twin"_s);
        second.directory = u"/mods/zz_twin"_s;
        const QList<QcModStatus> mods = QcModLoader::resolve({pack(u"twin"_s), second}, {}, base);
        QCOMPARE(loadedIds(mods), QStringList{u"twin"_s});
        QCOMPARE(mods.size(), 2);
        QVERIFY(mods[1].reason.contains(u"already uses"_s));
    }

    void manifestsAreValidated_data()
    {
        QTest::addColumn<QByteArray>("manifest");

        QTest::newRow("not json") << QByteArray("{ nope");
        QTest::newRow("no id") << QByteArray(R"({"version":"1.0"})");
        QTest::newRow("bad id") << QByteArray(R"({"id":"Bad Id","version":"1.0"})");
        QTest::newRow("no version") << QByteArray(R"({"id":"m"})");
        QTest::newRow("bad version") << QByteArray(R"({"id":"m","version":"soon"})");
        QTest::newRow("dependencies not a list") << QByteArray(R"({"id":"m","version":"1","dependencies":"qtcraft"})");
        QTest::newRow("self dependency") << QByteArray(R"({"id":"m","version":"1","dependencies":["m"]})");
        QTest::newRow("bad min version") << QByteArray(R"({"id":"m","version":"1","dependencies":[{"id":"qtcraft","minVersion":"x"}]})");
    }

    void manifestsAreValidated()
    {
        QFETCH(QByteArray, manifest);
        QTemporaryDir dir;
        writeText(dir.filePath(u"broken/pack.json"_s), manifest);
        copyExampleMod(dir.filePath(u"rubies"_s));

        const QList<QcModStatus> mods = QcModLoader::discover(dir.path());
        QCOMPARE(mods.size(), 2);
        QCOMPARE(loadedIds(mods), QStringList{u"rubies"_s});
        QCOMPARE(mods[1].state, QcModStatus::State::Failed);
        QVERIFY(!mods[1].reason.isEmpty());
        QVERIFY(mods[1].info.directory.endsWith(u"broken"_s));
    }

    void specialFilesAndLinksAreNotRead()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));

        // A pipe would block forever, a device would never end.
        QDir().mkpath(dir.filePath(u"pipe"_s));
        QCOMPARE(::mkfifo(QFile::encodeName(dir.filePath(u"pipe/pack.json"_s)).constData(), 0600), 0);
        QDir().mkpath(dir.filePath(u"zero"_s));
        QVERIFY(QFile::link(u"/dev/zero"_s, dir.filePath(u"zero/pack.json"_s)));

        // A linked mod folder and a linked texture could reach files outside the pack.
        QVERIFY(QFile::link(dir.filePath(u"rubies"_s), dir.filePath(u"linked"_s)));
        writeText(dir.filePath(u"outside.txt"_s), "secret");
        QVERIFY(QFile::link(dir.filePath(u"outside.txt"_s), dir.filePath(u"rubies/textures/leak.png"_s)));

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loadedIds(loaded->mods), QStringList{u"rubies"_s});
        QCOMPARE(loaded->mods.size(), 4);

        const QList<QcModStatus> others = loaded->mods.mid(1);
        for (const QcModStatus &mod : others) {
            QCOMPARE(mod.state, QcModStatus::State::Failed);
        }
        QVERIFY(std::ranges::any_of(loaded->mods, [](const QcModStatus &mod) { return mod.reason.contains(u"symbolic link"_s); }));
        QVERIFY(!loaded->content->assets().texture(u"rubies:leak"_s));
        QVERIFY(loaded->content->blockId(u"rubies:ruby_ore"_s));
#else
        QSKIP("POSIX pipes and links only");
#endif
    }

    void versionsAreComparedByValue()
    {
        QcContentPackInfo user = pack(u"user"_s, {u"lib"_s});
        user.minimumVersions.insert(u"lib"_s, QcContentPackLoader::parseVersion(u"1.0"_s));
        const QList<QcModStatus> mods = QcModLoader::resolve({pack(u"lib"_s, {}, u"1"_s), user}, {}, base);
        QCOMPARE(loadedIds(mods), (QStringList{u"lib"_s, u"user"_s}));

        QCOMPARE(QcContentPackLoader::parseVersion(u"1.0.0"_s), QcContentPackLoader::parseVersion(u"1"_s));
        QVERIFY(QcContentPackLoader::parseVersion(u"1.0beta"_s).isNull());
        QVERIFY(QcContentPackLoader::parseVersion(u""_s).isNull());
        QVERIFY(!QcContentPackLoader::isValidPackId(u".."_s));
        QVERIFY(!QcContentPackLoader::isValidPackId(u"a..b"_s));
        QVERIFY(!QcContentPackLoader::isValidPackId(u"a/b"_s));
        QVERIFY(QcContentPackLoader::isValidPackId(u"my_mod-2.x"_s));
    }

    void modsBeyondWhatClientsAcceptAreSkippedWithDependents()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));

        // Nine textures of almost 4 MiB exceed the 32 MiB content sync frame.
        writeJson(dir.filePath(u"huge/pack.json"_s), {{u"id"_s, u"huge"_s}, {u"version"_s, u"1"_s}});
        const QByteArray filler(int(QcContentPackLoader::maxContentFileBytes - 16), 'x');
        for (int i = 0; i < 9; ++i) {
            writeText(dir.filePath(u"huge/textures/t%1.png"_s.arg(i)), filler);
        }
        writeJson(dir.filePath(u"needs_huge/pack.json"_s),
                  {{u"id"_s, u"needs_huge"_s}, {u"version"_s, u"1"_s}, {u"dependencies"_s, QJsonArray{u"huge"_s}}});

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loadedIds(loaded->mods), QStringList{u"rubies"_s});
        QVERIFY(find(loaded->mods, u"huge"_s)->reason.contains(u"MiB"_s));
        QVERIFY(find(loaded->mods, u"needs_huge"_s)->reason.contains(u"huge"_s));
        QVERIFY(!loaded->content->assets().texture(u"huge:t0"_s));
        QCOMPARE(loaded->content->packs().size(), 2);
    }

    void oversizedModsFailBeforeTheyAreRead()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;

        // Hard links to one file take no space on disk but would be read again and again.
        writeJson(dir.filePath(u"links/pack.json"_s), {{u"id"_s, u"links"_s}, {u"version"_s, u"1"_s}});
        const QByteArray filler(qsizetype(1024) * 1024, 'x');
        writeText(dir.filePath(u"links/textures/t0.png"_s), filler);
        for (int i = 1; i < 64; ++i) {
            QCOMPARE(::link(QFile::encodeName(dir.filePath(u"links/textures/t0.png"_s)).constData(),
                            QFile::encodeName(dir.filePath(u"links/textures/t%1.png"_s.arg(i))).constData()),
                     0);
        }

        // Clients also limit what they accept beyond the byte count, e.g. translations per language: a
        // file with more is skipped before its table is built (decision E-371).
        QJsonObject many;
        for (int i = 0; i < 70000; ++i) {
            many.insert(u"k%1"_s.arg(i), u"v"_s);
        }
        writeJson(dir.filePath(u"words/pack.json"_s), {{u"id"_s, u"words"_s}, {u"version"_s, u"1"_s}});
        writeJson(dir.filePath(u"words/lang/en.json"_s), many);
        writeJson(dir.filePath(u"long/pack.json"_s), {{u"id"_s, u"long"_s}, {u"version"_s, u"1"_s}, {u"name"_s, QString(300, u'n')}});

        // A linked worldgen folder is not followed.
        writeJson(dir.filePath(u"outside/terrain.json"_s), {{u"seaLevel"_s, 3}});
        writeJson(dir.filePath(u"wg/pack.json"_s), {{u"id"_s, u"wg"_s}, {u"version"_s, u"1"_s}});
        QVERIFY(QFile::link(dir.filePath(u"outside"_s), dir.filePath(u"wg/worldgen"_s)));
        QElapsedTimer timer;
        timer.start();
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QVERIFY(timer.elapsed() < 20000);
        QVERIFY(find(loaded->mods, u"links"_s)->reason.contains(u"MiB left"_s));
        QVERIFY(find(loaded->mods, u"words"_s)->warnings.join(u'\n').contains(u"more than 65536 translations in one file"_s));
        QVERIFY(!loaded->content->translations().has(u"k1"_s, u"en"_s));
        QCOMPARE(loadedIds(loaded->mods), (QStringList{u"wg"_s, u"words"_s}));
        QVERIFY(std::ranges::any_of(loaded->mods, [](const QcModStatus &mod) { return mod.reason.contains(u"too long"_s); }));
        QVERIFY(loaded->content->worldgen().terrainSettings().seaLevel != 3);
#else
        QSKIP("POSIX links only");
#endif
    }

    void allModsShareOneReadBudget()
    {
#ifdef Q_OS_UNIX
        // Each mod alone fits, together they would read far more than any client can take.
        QTemporaryDir dir;
        const QByteArray filler(qsizetype(4) * 1024 * 1024 - 64, ' ');
        writeText(dir.filePath(u"source.json"_s), "{}" + filler);
        for (int m = 0; m < 6; ++m) {
            const QString mod = dir.filePath(u"m%1"_s.arg(m));
            writeJson(mod + u"/pack.json"_s, {{u"id"_s, u"m%1"_s.arg(m)}, {u"version"_s, u"1"_s}});
            QDir().mkpath(mod + u"/blocks"_s);
            for (int i = 0; i < 4; ++i) {
                QCOMPARE(::link(QFile::encodeName(dir.filePath(u"source.json"_s)).constData(),
                                QFile::encodeName(mod + u"/blocks/b%1.json"_s.arg(i)).constData()),
                         0);
            }
        }

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());

        // 16 MiB per mod: the first four fit the 64 MiB budget, the rest do not.
        QCOMPARE(loadedIds(loaded->mods), (QStringList{u"m0"_s, u"m1"_s, u"m2"_s, u"m3"_s}));
        QVERIFY(find(loaded->mods, u"m4"_s)->reason.contains(u"left for mods"_s));
#else
        QSKIP("POSIX links only");
#endif
    }

    void worldGenerationCostIsLimited()
    {
        QTemporaryDir dir;
        writeJson(dir.filePath(u"ores/pack.json"_s), {{u"id"_s, u"ores"_s}, {u"version"_s, u"1"_s}});
        for (int i = 0; i < 20; ++i) {
            writeJson(dir.filePath(u"ores/ores/o%1.json"_s.arg(i)),
                      {{u"name"_s, u"o%1"_s.arg(i)}, {u"block"_s, u"qtcraft:stone"_s}, {u"replaces"_s, u"qtcraft:dirt"_s},
                       {u"veinsPerChunk"_s, 64}, {u"veinSize"_s, 16}});
        }
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QVERIFY(find(loaded->mods, u"ores"_s)->reason.contains(u"too many ores"_s));
        QVERIFY(std::ranges::none_of(loaded->content->worldgen().ores(), [](const QcOreDefinition &ore) { return ore.name.startsWith(u"ores:"_s); }));

        // Pools: the base game's two attempts plus 7 × 8 fit 64 per chunk, one more mod does not.
        writeJson(dir.filePath(u"pools/pack.json"_s), {{u"id"_s, u"pools"_s}, {u"version"_s, u"1"_s}});
        writeJson(dir.filePath(u"more/pack.json"_s), {{u"id"_s, u"more"_s}, {u"version"_s, u"1"_s}, {u"dependencies"_s, QJsonArray{u"pools"_s}}});
        for (int i = 0; i < 8; ++i) {
            writeJson(dir.filePath(i < 7 ? u"pools/features/p%1.json"_s.arg(i) : u"more/features/p%1.json"_s.arg(i)),
                      {{u"name"_s, u"p%1"_s.arg(i)}, {u"type"_s, u"pool"_s}, {u"block"_s, u"qtcraft:lava"_s}, {u"attempts"_s, 8}});
        }
        const QcModLoader::Loaded pools = QcModLoader::load(dir.path());
        QVERIFY(pools.has_value());
        QVERIFY2(find(pools->mods, u"pools"_s)->reason.isEmpty(), qPrintable(find(pools->mods, u"pools"_s)->reason));
        QVERIFY(find(pools->mods, u"more"_s)->reason.contains(u"too many pools"_s));
    }

    void onlyTheModThatClientsRejectIsDropped()
    {
        QTemporaryDir dir;

        // Each language file alone is fine; together they exceed what a client takes per language.
        const auto words = [](int first, int count) {
            QJsonObject entries;
            for (int i = first; i < first + count; ++i) {
                entries.insert(u"k%1"_s.arg(i), u"v"_s);
            }
            return entries;
        };
        writeJson(dir.filePath(u"a0/pack.json"_s), {{u"id"_s, u"a0"_s}, {u"version"_s, u"1"_s}});
        writeJson(dir.filePath(u"a0/lang/en.json"_s), words(0, 40000));
        writeJson(dir.filePath(u"a1/pack.json"_s), {{u"id"_s, u"a1"_s}, {u"version"_s, u"1"_s}});
        writeJson(dir.filePath(u"a1/lang/en.json"_s), words(40000, 30000));
        for (const QString &id : {u"a2"_s, u"a3"_s}) {
            writeJson(dir.filePath(id + u"/pack.json"_s), {{u"id"_s, id}, {u"version"_s, u"1"_s}});
            writeJson(dir.filePath(id + u"/blocks/b.json"_s), {{u"name"_s, u"b"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:stone"_s}}}});
        }
        writeJson(dir.filePath(u"a4/pack.json"_s),
                  {{u"id"_s, u"a4"_s}, {u"version"_s, u"1"_s}, {u"dependencies"_s, QJsonArray{u"a1"_s}}});

        // A biome with too many feature placements is skipped like any broken definition.
        QJsonArray placements;
        for (int i = 0; i < 65; ++i) {
            placements.append(QJsonObject{{u"feature"_s, u"qtcraft:oak_tree"_s}, {u"density"_s, 0.0}});
        }
        writeJson(dir.filePath(u"a2/biomes/crowded.json"_s), {{u"name"_s, u"crowded"_s},
                   {u"surface"_s, u"qtcraft:grass_block"_s},
                   {u"subsurface"_s, u"qtcraft:dirt"_s},
                   {u"underwater"_s, u"qtcraft:sand"_s},
                   {u"beach"_s, u"qtcraft:sand"_s},
                   {u"features"_s, placements}});
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        const QStringList warnings = loaded ? loaded->warnings : QStringList();
        QVERIFY(loaded.has_value());
        QCOMPARE(loadedIds(loaded->mods), (QStringList{u"a0"_s, u"a2"_s, u"a3"_s}));
        QVERIFY(find(loaded->mods, u"a1"_s)->reason.contains(u"clients would reject"_s));
        QVERIFY(find(loaded->mods, u"a4"_s)->reason.contains(u"'a1'"_s));
        QVERIFY(loaded->content->blockId(u"a3:b"_s));
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) { return w.contains(u"more than 64"_s); }));

        // The result is what a client accepts.
        QByteArray encoded;
        QDataStream out(&encoded, QIODevice::WriteOnly);
        loaded->content->writeTo(out);
        QDataStream in(encoded);
        QVERIFY(QcContentRegistry::readFrom(in).has_value());
    }

    void refusedModsUseUpTheReadBudgetToo()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;

        // About 4 MB of whitespace around a tiny object: cheap to parse, expensive to read.
        writeText(dir.filePath(u"padding.json"_s), "{\"k\":\"v\"}" + QByteArray(qsizetype(3900) * 1024, ' '));
        for (int m = 0; m < 5; ++m) {
            const QString mod = dir.filePath(u"f%1"_s.arg(m));
            writeJson(mod + u"/pack.json"_s, {{u"id"_s, u"f%1"_s.arg(m)}, {u"version"_s, u"1"_s}});
            // Too many ore blocks per chunk: refused after reading.
            writeJson(mod + u"/ores/heavy.json"_s, {{u"name"_s, u"heavy"_s}, {u"block"_s, u"qtcraft:stone"_s},
                                                    {u"replaces"_s, u"qtcraft:dirt"_s}, {u"veinsPerChunk"_s, 256},
                                                    {u"veinSize"_s, 64}});
            QDir().mkpath(mod + u"/lang"_s);
            for (const char *language : {"en", "de", "fr", "es", "it"}) {
                QCOMPARE(::link(QFile::encodeName(dir.filePath(u"padding.json"_s)).constData(),
                                QFile::encodeName(mod + u"/lang/"_s + QString::fromLatin1(language) + u".json"_s).constData()),
                         0);
            }
        }

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QVERIFY(loadedIds(loaded->mods).isEmpty());

        // Three mods of about 19.5 MB were read and refused; the fourth no longer fits the 64 MiB.
        QVERIFY(find(loaded->mods, u"f2"_s)->reason.contains(u"too many ores"_s));
        QVERIFY(find(loaded->mods, u"f3"_s)->reason.contains(u"left for mods"_s));
#else
        QSKIP("POSIX links only");
#endif
    }

    void severalModsThatClientsRejectAreAllFound()
    {
        QTemporaryDir dir;
        const auto words = [](int first, int count) {
            QJsonObject entries;
            for (int i = first; i < first + count; ++i) {
                entries.insert(u"k%1"_s.arg(i), u"v"_s);
            }
            return entries;
        };

        // b0 fills the English table almost up (500 entries stay free next to the base game's); b1 and
        // b3 each tip it over, b2 and b4 are harmless, b5 depends on b3.
        const QcResult<QcBaseContent::Loaded> base = QcTestUtil::baseContent();
        QVERIFY(base.has_value());
        const int baseEntries = int(base->registry->translations().count(u"en"_s));

        writeJson(dir.filePath(u"b0/pack.json"_s), {{u"id"_s, u"b0"_s}, {u"version"_s, u"1"_s}});
        writeJson(dir.filePath(u"b0/lang/en.json"_s), words(0, int(QcStreamLimits::maxDefinitions) - baseEntries - 500));
        for (const QString &id : {u"b1"_s, u"b3"_s}) {
            writeJson(dir.filePath(id + u"/pack.json"_s), {{u"id"_s, id}, {u"version"_s, u"1"_s}});
            writeJson(dir.filePath(id + u"/lang/en.json"_s), words(id == u"b1"_s ? 70000 : 80000, 1000));
        }
        for (const QString &id : {u"b2"_s, u"b4"_s}) {
            writeJson(dir.filePath(id + u"/pack.json"_s), {{u"id"_s, id}, {u"version"_s, u"1"_s}});
            writeJson(dir.filePath(id + u"/lang/de.json"_s), words(0, 10));
        }
        writeJson(dir.filePath(u"b5/pack.json"_s),
                  {{u"id"_s, u"b5"_s}, {u"version"_s, u"1"_s}, {u"dependencies"_s, QJsonArray{u"b3"_s}}});

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loadedIds(loaded->mods), (QStringList{u"b0"_s, u"b2"_s, u"b4"_s}));
        QVERIFY(find(loaded->mods, u"b1"_s)->reason.contains(u"clients would reject"_s));
        QVERIFY(find(loaded->mods, u"b3"_s)->reason.contains(u"clients would reject"_s));
        QVERIFY(find(loaded->mods, u"b5"_s)->reason.contains(u"'b3'"_s));
        QCOMPARE(loaded->content->packs().size(), 4);
    }

    void itemsCreatedForBlocksCountAsWell()
    {
        QTemporaryDir dir;

        // Long block names: each block gets an item with its name again, which a check of the blocks
        // alone would miss.
        writeJson(dir.filePath(u"long/pack.json"_s), {{u"id"_s, u"long"_s}, {u"version"_s, u"1"_s}});
        for (int i = 0; i < 700; ++i) {
            const QString name = u"b%1_"_s.arg(i) + QString(10000, u'x');
            writeJson(dir.filePath(u"long/blocks/b%1.json"_s.arg(i)),
                      {{u"name"_s, name}, {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:stone"_s}}}});
        }

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QVERIFY2(find(loaded->mods, u"long"_s)->reason.contains(u"exceeds"_s), qPrintable(find(loaded->mods, u"long"_s)->reason));

        QByteArray encoded;
        QDataStream out(&encoded, QIODevice::WriteOnly);
        loaded->content->writeTo(out);
        QDataStream in(encoded);
        QVERIFY(QcContentRegistry::readFrom(in).has_value());
    }

    void atMost64ModsAreActive()
    {
        QTemporaryDir dir;
        for (int m = 0; m < 66; ++m) {
            const QString id = u"m%1"_s.arg(m, 2, 10, QChar(u'0'));
            writeJson(dir.filePath(id + u"/pack.json"_s), {{u"id"_s, id}, {u"version"_s, u"1"_s}});
        }

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loadedIds(loaded->mods).size(), QcModLoader::maxLoadedMods);
        QVERIFY(find(loaded->mods, u"m65"_s)->reason.contains(u"at most 64 mods are loaded"_s));
    }

    void exampleModAddsBlockItemRecipeAndMob()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        const QStringList warnings = loaded ? loaded->warnings : QStringList();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        const QcContentRegistry &content = *loaded->content;

        QCOMPARE(content.packs().size(), 2);
        QCOMPARE(content.packs()[1], (QcPackReference{u"rubies"_s, u"Rubies"_s, u"1.0.0"_s}));
        QVERIFY(content.blockId(u"rubies:ruby_ore"_s));
        QVERIFY(content.blockId(u"rubies:ruby_block"_s));
        QVERIFY(content.itemId(u"rubies:ruby_block"_s)); // block item created for mod blocks too
        QVERIFY(content.itemId(u"rubies:ruby_sword"_s));
        QVERIFY(content.mobIndex(u"rubies:crystal_beetle"_s));
        QVERIFY(std::ranges::any_of(content.worldgen().ores(), [](const QcOreDefinition &ore) { return ore.block == u"rubies:ruby_ore"_s; }));
        QCOMPARE(content.translations().translate(u"item.rubies.ruby"_s, u"de"_s), u"Rubin"_s);
        QVERIFY(content.assets().texture(u"rubies:ruby"_s));

        // Building parts from the mod's material with the base families (walls left out) and the
        // mod's own pillar family, which also applies to the base stones; the excluded ore gets none.
        QVERIFY(content.blockId(u"rubies:ruby_stairs"_s));
        QVERIFY(content.blockId(u"rubies:ruby_slab"_s));
        QVERIFY(!content.blockId(u"rubies:ruby_wall"_s));
        QVERIFY(content.blockId(u"rubies:ruby_pillar"_s));
        QVERIFY(content.blockId(u"qtcraft:cobblestone_pillar"_s));
        QVERIFY(!content.blockId(u"rubies:ruby_ore_stairs"_s));
        QCOMPARE(content.block(*content.blockId(u"rubies:ruby_stairs"_s)).toolLevel, 3);
        QCOMPARE(content.translations().translate(content.block(*content.blockId(u"rubies:ruby_pillar"_s)).translationKey, u"de"_s), u"Rubinsäule"_s);
        QCOMPARE(content.state(*content.blockId(u"rubies:ruby_pillar"_s)).boxes.size(), 3);

        // A glowing block with its own inline shape.
        const QcBlockId lantern = *content.blockId(u"rubies:ruby_lantern"_s);
        QCOMPARE(content.block(lantern).lightLevel, 11);
        QVERIFY(!content.state(lantern).fullCube);

        // The mod's recipes work with the shared crafting rules, also mixed with base items.
        const QcItemId ruby = *content.itemId(u"rubies:ruby"_s);
        QList<QcItemStack> grid(9, QcItemStack{ruby, 1, 0});
        std::optional<qsizetype> recipe = QcCrafting::match(content, grid, 3);
        QVERIFY(recipe);
        QCOMPARE(content.recipes()[*recipe].resultItem, u"rubies:ruby_block"_s);
        grid = QList<QcItemStack>(9);
        grid[1] = {ruby, 1, 0};
        grid[4] = {ruby, 1, 0};
        grid[7] = {*content.itemId(u"qtcraft:stick"_s), 1, 0};
        recipe = QcCrafting::match(content, grid, 3);

        QVERIFY(recipe);
        QCOMPARE(content.recipes()[*recipe].resultItem, u"rubies:ruby_sword"_s);

        // A smelting recipe and a fuel from the mod work in the base game's furnace.
        const QcSmeltingRecipe *sifting = content.smeltingRecipe(u"smelting"_s, u"qtcraft:gravel"_s);
        QVERIFY(sifting);
        QCOMPARE(sifting->resultItem, u"rubies:ruby"_s);
        QCOMPARE(sifting->seconds, 30.0);
        const QcItemId ember = *content.itemId(u"rubies:ember_shard"_s);
        QCOMPARE(content.item(ember).fuelSeconds, 120.0);

        auto shared = std::make_shared<const QcContentRegistry>(content);
        QcTestMachines machines(shared);
        QcTestWorld world(content);
        const QcBlockPos pos{0, 64, 0};
        world.set(pos, *content.blockId(u"qtcraft:furnace"_s));
        QVERIFY(machines.add(pos, *content.blockId(u"qtcraft:furnace"_s)));
        machines.find(pos)->stacks = {QcItemStack{*content.itemId(u"qtcraft:gravel"_s), 5, 0}, QcItemStack{ember, 1, 0}, QcItemStack{}};
        for (int i = 0; i < 30 * 20 + 1; ++i) {
            machines.tick(world, 0.05);
        }
        QCOMPARE(machines.find(pos)->stacks[2], (QcItemStack{ruby, 1, 0}));
        QVERIFY(machines.find(pos)->stacks[1].isEmpty());
        QCOMPARE(machines.find(pos)->burnSeconds, 120.0);
    }

    void exampleModShowsInTheShowcase()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));

        // The mod's demo structure, its creature and its blocks join the showcase without extra data.
        const QcShowcaseGenerator generator(loaded->content);
        QVERIFY2(generator.warnings().isEmpty(), qPrintable(generator.warnings().join(u'\n')));
        const QList<QcShowcaseAreaInfo> &areas = generator.plan().areas;
        QVERIFY(std::ranges::any_of(areas, [](const QcShowcaseAreaInfo &area) { return area.name == u"demo-ruby_shrine"_s; }));
        QVERIFY(std::ranges::any_of(generator.setup().mobs, [](const QcWorldSetup::Mob &mob) { return mob.mob == u"rubies:crystal_beetle"_s; }));
        bool rubyLabel = false;
        for (const QList<QcWorldLabel> &labels : generator.plan().labelsByChunk) {
            rubyLabel = rubyLabel || std::ranges::any_of(labels, [](const QcWorldLabel &label) { return label.text == u"block.rubies.ruby_ore"_s; });
        }
        QVERIFY(rubyLabel);
    }

    void exampleModAddsLiquidPlantAndFallingBlock()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        const QStringList warnings = loaded ? loaded->warnings : QStringList();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));

        auto content = loaded->content;
        const auto state = [&](const QString &name) { return *content->stateId(name); };
        QcTestWorld world(*content);
        world.fillBox({-8, 63, -8}, {8, 63, 8}, state(u"qtcraft:stone"_s));

        // Molten ruby flows four blocks wide, every 15 ticks, and fills its own bucket.
        const QcBlockDefinition &molten = content->block(state(u"rubies:molten_ruby"_s));
        QCOMPARE(molten.flow.distance, 4);
        QCOMPARE(molten.flow.bucket, u"rubies:molten_ruby_bucket"_s);
        QCOMPARE(content->item(*content->itemId(u"rubies:molten_ruby_bucket"_s)).emptiesTo, u"qtcraft:bucket"_s);

        QcLiquidFlow flow(content);
        world.set({0, 64, 0}, state(u"rubies:molten_ruby"_s));
        flow.schedule(world, {0, 64, 0}, 0);
        for (quint64 tick = 1; tick <= 300; ++tick) {
            flow.tick(tick, world, [&world](QcBlockPos pos, QcBlockId block) { world.set(pos, block); });
        }

        QCOMPARE(content->stateName(*world.blockAt({4, 64, 0})), u"rubies:molten_ruby[falling=false,level=4]"_s);
        QCOMPARE(*world.blockAt({5, 64, 0}), qcAirBlockId);

        // Next to base water its flow turns into the mod's ruby sand, which falls.
        world.set({1, 65, 0}, state(u"qtcraft:water"_s));
        const std::optional<QcBlockId> turned = QcLiquidFlow::reaction(world, {1, 64, 0}, *world.blockAt({1, 64, 0}));
        QCOMPARE(turned, std::optional<QcBlockId>(state(u"rubies:ruby_sand"_s)));
        world.set({0, 70, 0}, state(u"rubies:ruby_sand"_s));
        QVERIFY(QcFallingBlockManager::shouldFall(world, {0, 70, 0}));

        // The ruby rose grows on grass only and is part of the plains the mod extends.
        world.set({3, 70, 3}, state(u"qtcraft:grass_block"_s));
        world.set({3, 71, 3}, state(u"rubies:ruby_rose"_s));
        QVERIFY(QcBlockRules::isSupported(world, {3, 71, 3}, state(u"rubies:ruby_rose"_s)));
        world.set({3, 70, 3}, state(u"qtcraft:stone"_s));
        QVERIFY(!QcBlockRules::isSupported(world, {3, 71, 3}, state(u"rubies:ruby_rose"_s)));

        const auto plains = std::ranges::find(content->worldgen().biomes(), u"qtcraft:plains"_s, &QcBiomeDefinition::name);
        QVERIFY(plains != content->worldgen().biomes().end());
        // Among the flowers of its vegetation (decision E-814), next to the base game's grass.
        QVERIFY(std::ranges::any_of(plains->vegetation.flowers.plants, [](const QcVegetationPlant &plant) {
            return plant.block == u"rubies:ruby_rose"_s;
        }));
        QVERIFY(std::ranges::any_of(plains->vegetation.grass.plants, [](const QcVegetationPlant &plant) {
            return plant.block == u"qtcraft:tall_grass"_s;
        }));

        // Pools of molten ruby sink into cave floors next to the base game's lava pools.
        const QcTerrainGenerator generator(content, 12345);
        QVERIFY2(generator.warnings().isEmpty(), qPrintable(generator.warnings().join(u'\n')));
        const QcBlockId moltenSource = state(u"rubies:molten_ruby"_s);
        const QcBlockId lavaSource = state(u"qtcraft:lava"_s);
        const QHash<QcBlockId, int> cells = deepCells(generator, {moltenSource, lavaSource});
        QVERIFY(cells.value(moltenSource) > 0);
        QVERIFY(cells.value(lavaSource) > 0);
    }

    void modsCanAddTheirOwnMachines()
    {
        QTemporaryDir dir;
        const QString mill = dir.filePath(u"mill"_s);
        writeJson(mill + u"/pack.json"_s, {{u"id"_s, u"mill"_s}, {u"version"_s, u"1.0"_s}});
        writeJson(mill + u"/blocks/millstone.json"_s,
                  {{u"name"_s, u"millstone"_s},
                   {u"machine"_s, QJsonObject{{u"category"_s, u"milling"_s}, {u"speed"_s, 4.0}}},
                   {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:stone"_s}}}});
        writeJson(mill + u"/items/flour.json"_s, {{u"name"_s, u"flour"_s}, {u"texture"_s, u"qtcraft:sand"_s}});
        writeJson(mill + u"/smelting/flour.json"_s,
                  {{u"name"_s, u"flour"_s}, {u"category"_s, u"milling"_s}, {u"input"_s, u"qtcraft:gravel"_s},
                   {u"result"_s, QJsonObject{{u"item"_s, u"flour"_s}, {u"count"_s, 2}}}, {u"seconds"_s, 2.0}});

        // A block that burns: its item inherits the fuel time.
        writeJson(mill + u"/blocks/dry_bush.json"_s,
                  {{u"name"_s, u"dry_bush"_s}, {u"fuelSeconds"_s, 1.0}, {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:sand"_s}}}});
        writeJson(mill + u"/smelting/broken.json"_s, {{u"name"_s, u"broken"_s}, {u"input"_s, u"qtcraft:sand"_s}});

        // Raw iron already smelts in the base game; a second recipe under another name is never used.
        writeJson(mill + u"/smelting/iron_flour.json"_s,
                  {{u"name"_s, u"iron_flour"_s}, {u"input"_s, u"qtcraft:raw_iron"_s},
                   {u"result"_s, QJsonObject{{u"item"_s, u"flour"_s}}}});
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        const QStringList warnings = loaded ? loaded->warnings : QStringList();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) { return w.contains(u"broken.json"_s); }));
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) {
            return w.contains(u"mill:iron_flour is never used"_s);
        }));

        const std::shared_ptr<const QcContentRegistry> content = loaded->content;
        QCOMPARE(content->smeltingRecipe(u"smelting"_s, u"qtcraft:raw_iron"_s)->resultItem, u"qtcraft:iron_ingot"_s);
        const QcBlockId millstone = *content->blockId(u"mill:millstone"_s);
        QCOMPARE(content->block(millstone).machine.category, u"milling"_s);

        // The base furnace does not mill, the millstone does not smelt.
        QVERIFY(!content->smeltingRecipe(u"smelting"_s, u"qtcraft:gravel"_s));
        QVERIFY(content->smeltingRecipe(u"milling"_s, u"qtcraft:gravel"_s));
        QVERIFY(!content->smeltingRecipe(u"milling"_s, u"qtcraft:raw_iron"_s));

        QcTestMachines machines(content);
        QcTestWorld world(*content);
        const QcBlockPos pos{3, 64, 3};
        world.set(pos, millstone);
        QVERIFY(machines.add(pos, millstone));
        const QcItemId bush = *content->itemId(u"mill:dry_bush"_s);
        QCOMPARE(content->item(bush).fuelSeconds, 1.0);
        machines.find(pos)->stacks = {QcItemStack{*content->itemId(u"qtcraft:gravel"_s), 3, 0}, QcItemStack{bush, 1, 0}, QcItemStack{}};

        // Speed 4: a bush (1 s of fuel) burns out after 5 ticks, half way through the 2 s recipe; the
        // progress then falls back. With coal, one round of 2 s takes 10 ticks.
        for (int i = 0; i < 20; ++i) {
            machines.tick(world, 0.05);
        }
        QVERIFY(machines.find(pos)->stacks[2].isEmpty());
        QCOMPARE(machines.find(pos)->stacks[0].count, 3);

        machines.find(pos)->stacks[1] = {*content->itemId(u"qtcraft:coal"_s), 1, 0};
        for (int i = 0; i < 11; ++i) {
            machines.tick(world, 0.05);
        }
        QCOMPARE(machines.find(pos)->stacks[2], (QcItemStack{*content->itemId(u"mill:flour"_s), 2, 0}));
        QCOMPARE(machines.find(pos)->stacks[0].count, 2);
    }

    void modsOverrideEarlierPacksAndBrokenEntriesAreSkipped()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));
        const QString tweak = dir.filePath(u"tweak"_s);
        writeJson(tweak + u"/pack.json"_s,
                  {{u"id"_s, u"tweak"_s}, {u"version"_s, u"0.2"_s}, {u"dependencies"_s, QJsonArray{u"rubies"_s}}});

        // Replaces a base block, overrides a mod item, renames a base item and brings one broken file.
        // A replacement is a complete definition; nothing is merged with the old one.
        writeJson(tweak + u"/blocks/glass.json"_s, {{u"name"_s, u"qtcraft:glass"_s},
                                                    {u"render"_s, u"cutout"_s},
                                                    {u"hardness"_s, 9.0},
                                                    {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:glass"_s}}}});
        writeJson(tweak + u"/items/ruby.json"_s, {{u"name"_s, u"rubies:ruby"_s}, {u"maxStack"_s, 16}});
        writeJson(tweak + u"/lang/en.json"_s, {{u"item.qtcraft.stick"_s, u"Twig"_s}});
        writeText(tweak + u"/recipes/broken.json"_s, "{ not json");
        writeJson(tweak + u"/blocks/odd_stem.json"_s,
                  {{u"name"_s, u"tweak:odd_stem"_s}, {u"shape"_s, u"cross"_s}, {u"tillsInto"_s, u"tweak:no_soil"_s},
                   {u"states"_s, QJsonObject{{u"age"_s, QJsonArray{u"0"_s, u"1"_s}}}},
                   {u"crop"_s, QJsonObject{{u"fruit"_s, u"tweak:no_fruit"_s}, {u"fruitGround"_s, QJsonArray{u"qtcraft:dirt"_s}}}},
                   {u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:dirt"_s}}}});
        const QJsonObject boarPart{{u"from"_s, QJsonArray{-0.3, 0.0, -0.3}}, {u"to"_s, QJsonArray{0.3, 0.6, 0.3}}, {u"texture"_s, u"qtcraft:pig_skin"_s}};
        writeJson(tweak + u"/mobs/boar.json"_s, {{u"name"_s, u"tweak:boar"_s},
                                                 {u"model"_s, QJsonArray{boarPart}},
                                                 {u"breeding"_s, QJsonObject{{u"food"_s, QJsonArray{u"tweak:acorn"_s}}}}});
        writeJson(tweak + u"/recipes/ghost.json"_s,
                  {{u"name"_s, u"ghost"_s}, {u"type"_s, u"shapeless"_s}, {u"ingredients"_s, QJsonArray{u"nothing"_s}},
                   {u"result"_s, QJsonObject{{u"item"_s, u"qtcraft:stick"_s}}}});
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        const QStringList warnings = loaded ? loaded->warnings : QStringList();
        QVERIFY(loaded.has_value());
        const QcContentRegistry &content = *loaded->content;

        QCOMPARE(loadedIds(loaded->mods), (QStringList{u"rubies"_s, u"tweak"_s}));
        QCOMPARE(content.block(*content.blockId(u"qtcraft:glass"_s)).hardness, 9.0);
        QCOMPARE(content.item(*content.itemId(u"rubies:ruby"_s)).maxStack, 16);
        QCOMPARE(content.translations().translate(u"item.qtcraft.stick"_s, u"en"_s), u"Twig"_s);
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) { return w.contains(u"broken.json"_s); }));
        QVERIFY(std::ranges::none_of(warnings, [](const QString &w) { return w.contains(u"glass"_s); }));
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) { return w.contains(u"unknown item 'tweak:nothing'"_s); }));
        for (const QString &expected : {u"unknown fruit 'tweak:no_fruit'"_s, u"unknown tillsInto 'tweak:no_soil'"_s,
                                        u"unknown breeding food 'tweak:acorn'"_s}) {
            QVERIFY2(std::ranges::any_of(warnings, [&expected](const QString &w) { return w.contains(expected); }), qPrintable(warnings.join(u'\n')));
        }
    }

    void disabledModsAreRemembered()
    {
        QTemporaryDir dir;
        copyExampleMod(dir.filePath(u"rubies"_s));
        QVERIFY(QcModLoader::setModEnabled(dir.path(), u"rubies"_s, false).has_value());
        QCOMPARE(QcModLoader::discover(dir.path()).constFirst().state, QcModStatus::State::Disabled);
        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY(loaded.has_value());
        QVERIFY(!loaded->content->blockId(u"rubies:ruby_ore"_s));

        QVERIFY(QcModLoader::setModEnabled(dir.path(), u"rubies"_s, true).has_value());
        QCOMPARE(QcModLoader::discover(dir.path()).constFirst().state, QcModStatus::State::Loaded);

        // No mods directory at all is fine.
        QVERIFY(QcModLoader::discover(dir.filePath(u"missing"_s)).isEmpty());
        QVERIFY(QcModLoader::load(QString()).has_value());
    }

    void worldsSurviveARemovedMod()
    {
        QTemporaryDir mods;
        QTemporaryDir world;
        copyExampleMod(mods.filePath(u"rubies"_s));
        const std::shared_ptr<const QcContentRegistry> withMod = QcModLoader::load(mods.path())->content;

        QcServerConfig config;
        config.worldDirectory = world.path();
        const QcBlockPos pos{3, 70, 3};
        {
            QcServer server(config, withMod, std::make_unique<QcTestTerrainGenerator>(withMod));
            QVERIFY(server.openWorld().has_value());
            server.world().loadChunk(pos.chunk());
            QTRY_VERIFY(server.world().chunk(pos.chunk()));
            QVERIFY(server.world().setBlock(pos, *withMod->blockId(u"rubies:ruby_block"_s)));
            QVERIFY(server.world().setBlock(pos.offset(1, 0, 0), *withMod->blockId(u"qtcraft:glass"_s)));
            QVERIFY(server.save().has_value());
        }

        QcResult<QcWorldInfo> info = QcWorldStorage(world.path(), withMod).readInfo();
        QVERIFY(info.has_value());
        QCOMPARE(info->packs, withMod->packs());

        const std::shared_ptr<const QcContentRegistry> without = QcModLoader::load(QString())->content;
        QcServer server(config, without, std::make_unique<QcTestTerrainGenerator>(without));
        QVERIFY(server.openWorld().has_value());
        QVERIFY(std::ranges::any_of(server.openWarnings(), [](const QString &w) { return w.contains(u"Rubies"_s); }));

        // A joining player is told as well.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        QcRawClient player(std::move(clientEnd));
        server.addClient(std::move(serverEnd));
        player.hello(u"Visitor"_s);

        QTRY_VERIFY(std::ranges::any_of(player.chatLines, [](const QString &line) { return line.contains(u"Rubies"_s); }));
        QCOMPARE(server.world().blockAt(pos), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(server.world().blockAt(pos.offset(1, 0, 0)), without->blockId(u"qtcraft:glass"_s));
    }
};

QTEST_GUILESS_MAIN(TestMods)
#include "test_mods.moc"
