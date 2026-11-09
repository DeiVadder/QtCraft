// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcMusicLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

// A pack folder `id` below `root` with pack.json.
QString makePack(const QTemporaryDir &root, const QString &id)
{
    const QString directory = root.filePath(id);
    writeFile(QDir(directory).filePath(u"pack.json"_s), QJsonDocument(QJsonObject{{u"id"_s, id}, {u"name"_s, id}, {u"version"_s, u"1.0.0"_s}}).toJson());
    return directory;
}

// The description music/<name>.json of `pack`, and (with `withFile`) a stand-in music file.
void addTrack(const QString &pack, const QString &name, const QJsonObject &json, bool withFile = true)
{
    writeFile(QDir(pack).filePath(u"music/%1.json"_s.arg(name)), QJsonDocument(json).toJson());
    if (withFile) {
        writeFile(QDir(pack).filePath(u"music/%1.ogg"_s.arg(name)), "OggS");
    }
}

const QString attributionFormat = u"^'[^']+' by Scott Buckley - released under CC-BY 4\\.0\\. www\\.scottbuckley\\.com\\.au$"_s;

} // namespace

// The music tracks of the content packs (decision E-1136): descriptions, places, files, later packs
// and the biomes known by their ambience loops; and the base game's tracks themselves.
class TestMusicLibrary : public QObject
{
    Q_OBJECT

private slots:
    // Fixed places stay as they are, biomes and dimensions get the pack's namespace, anything
    // else is no place.
    void slotNames()
    {
        QCOMPARE(QcMusicLibrary::slotName(u"day"_s, u"p"_s), u"day"_s);
        QCOMPARE(QcMusicLibrary::slotName(u"menu"_s, u"p"_s), u"menu"_s);
        QCOMPARE(QcMusicLibrary::slotName(u"biome:glade"_s, u"p"_s), u"biome:p:glade"_s);
        QCOMPARE(QcMusicLibrary::slotName(u"biome:other:glade"_s, u"p"_s), u"biome:other:glade"_s);
        QCOMPARE(QcMusicLibrary::slotName(u"dimension:nether"_s, u"qtcraft"_s), u"dimension:qtcraft:nether"_s);
        QVERIFY(!QcMusicLibrary::slotName(u"dawn"_s, u"p"_s));
        QVERIFY(!QcMusicLibrary::slotName(u"biome:"_s, u"p"_s));
        QVERIFY(!QcMusicLibrary::slotName(u"biome::x"_s, u"p"_s));
        QVERIFY(!QcMusicLibrary::slotName(u"Day"_s, u"p"_s));
    }

    // A description with every field: the track, its places and its credit line.
    void readsATrack()
    {
        QTemporaryDir root;
        const QString pack = makePack(root, u"songs"_s);
        addTrack(pack, u"dawn_song"_s,
                 {{u"title"_s, u"Dawn Song"_s},
                  {u"artist"_s, u"Someone"_s},
                  {u"licence"_s, u"CC0-1.0"_s},
                  {u"attribution"_s, u"'Dawn Song' by Someone"_s},
                  {u"slots"_s, QJsonArray{u"day"_s, u"biome:glade"_s, u"dimension:nether"_s}}});

        const QcMusicLibrary library = QcMusicLibrary::load({pack});
        QVERIFY2(library.warnings().isEmpty(), qPrintable(library.warnings().join(u'\n')));
        QCOMPARE(library.tracks().size(), 1);
        const QcMusicTrack *track = library.track(u"songs:dawn_song"_s);
        QVERIFY(track);
        QCOMPARE(track->title, u"Dawn Song"_s);
        QCOMPARE(track->artist, u"Someone"_s);
        QCOMPARE(track->licence, u"CC0-1.0"_s);
        QCOMPARE(track->path, QDir(pack).filePath(u"music/dawn_song.ogg"_s));
        QCOMPARE(track->places, (QStringList{u"day"_s, u"biome:songs:glade"_s, u"dimension:songs:nether"_s}));
        QCOMPARE(library.tracksFor(u"biome:songs:glade"_s), QStringList{u"songs:dawn_song"_s});
        QVERIFY(library.tracksFor(u"night"_s).isEmpty());
        QCOMPARE(library.attributions(), QStringList{u"'Dawn Song' by Someone"_s});
    }

    // Unknown keys are reported in the log and ignored, unknown places are left out with a
    // warning; the track stays.
    void reportsUnknownKeysAndPlaces()
    {
        QTemporaryDir root;
        const QString pack = makePack(root, u"songs"_s);
        addTrack(pack, u"a"_s, {{u"slots"_s, QJsonArray{u"night"_s, u"dawn"_s}}, {u"tempo"_s, 80}});

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"music track .*a\\.json has an unknown key 'tempo', ignored"_s));
        const QcMusicLibrary library = QcMusicLibrary::load({pack});
        QCOMPARE(library.warnings().size(), 1);
        QVERIFY2(library.warnings().constFirst().contains(u"unknown place dawn"_s), qPrintable(library.warnings().constFirst()));
        QCOMPARE(library.tracksFor(u"night"_s), QStringList{u"songs:a"_s});
        // Without a title the file's name is the title.
        QCOMPARE(library.track(u"songs:a"_s)->title, u"a"_s);
    }

    // A track whose music file is missing, or whose places are no list, is left out with a
    // warning; one with another file name plays that file.
    void leavesOutBrokenTracks()
    {
        QTemporaryDir root;
        const QString pack = makePack(root, u"songs"_s);
        addTrack(pack, u"silent"_s, {{u"slots"_s, QJsonArray{u"day"_s}}}, false);
        addTrack(pack, u"listless"_s, {{u"slots"_s, u"day"_s}});
        addTrack(pack, u"renamed"_s, {{u"file"_s, u"other.ogg"_s}, {u"slots"_s, QJsonArray{u"day"_s}}}, false);
        writeFile(QDir(pack).filePath(u"music/other.ogg"_s), "OggS");

        const QcMusicLibrary library = QcMusicLibrary::load({pack});
        QCOMPARE(library.tracksFor(u"day"_s), QStringList{u"songs:renamed"_s});
        QCOMPARE(library.track(u"songs:renamed"_s)->path, QDir(pack).filePath(u"music/other.ogg"_s));
        QCOMPARE(library.warnings().size(), 2);
        QVERIFY(library.warnings().filter(u"silent.ogg is missing"_s).size() == 1);
        QVERIFY(library.warnings().filter(u"'slots' must be an array"_s).size() == 1);
    }

    // A later pack replaces a track of the same name; a pack without a manifest is skipped.
    void laterPacksReplaceTracks()
    {
        QTemporaryDir root;
        const QString first = makePack(root, u"songs"_s);
        addTrack(first, u"a"_s, {{u"title"_s, u"Old"_s}, {u"slots"_s, QJsonArray{u"day"_s}}});
        addTrack(first, u"b"_s, {{u"slots"_s, QJsonArray{u"night"_s}}});
        // A second folder with the same namespace: its track "a" has the same name.
        const QString second = root.filePath(u"override"_s);
        writeFile(QDir(second).filePath(u"pack.json"_s), QJsonDocument(QJsonObject{{u"id"_s, u"songs"_s}, {u"name"_s, u"x"_s}, {u"version"_s, u"1.0.0"_s}}).toJson());
        addTrack(second, u"a"_s, {{u"title"_s, u"New"_s}, {u"slots"_s, QJsonArray{u"cave"_s}}});

        const QcMusicLibrary library = QcMusicLibrary::load({first, root.filePath(u"missing"_s), second});
        QCOMPARE(library.tracks().size(), 2);
        QCOMPARE(library.tracks().constFirst().title, u"New"_s);
        QVERIFY(library.tracksFor(u"day"_s).isEmpty());
        QCOMPARE(library.tracksFor(u"cave"_s), QStringList{u"songs:a"_s});
        QCOMPARE(library.warnings().size(), 1);
    }

    // Biomes are known by their ambience loops, both with the pack's namespace.
    void knowsBiomesByTheirLoops()
    {
        QTemporaryDir root;
        const QString pack = makePack(root, u"wild"_s);
        const QJsonObject atmosphere{{u"ambience"_s, QJsonObject{{u"loop"_s, u"ambient.glade.loop"_s}}}};
        writeFile(QDir(pack).filePath(u"biomes/glade.json"_s), QJsonDocument(QJsonObject{{u"name"_s, u"glade"_s}, {u"atmosphere"_s, atmosphere}}).toJson());
        writeFile(QDir(pack).filePath(u"biomes/plain.json"_s), QJsonDocument(QJsonObject{{u"name"_s, u"plain"_s}}).toJson());

        const QcMusicLibrary library = QcMusicLibrary::load({pack});
        QCOMPARE(library.biomeOfLoop(u"wild:ambient.glade.loop"_s), u"wild:glade"_s);
        QVERIFY(library.biomeOfLoop(u"ambient.glade.loop"_s).isEmpty());
        QVERIFY(library.biomeOfLoop({}).isEmpty());
    }

    // The base game: every track has its file, its credit line in the form the licence asks for,
    // and every place of the base game has music, each nether biome its own.
    void baseGameHasMusicEverywhere()
    {
        const QcMusicLibrary library = QcMusicLibrary::load({QStringLiteral(QTCRAFT_SOURCE_DIR "/content/qtcraft")});
        QVERIFY2(library.warnings().isEmpty(), qPrintable(library.warnings().join(u'\n')));
        QCOMPARE(library.tracks().size(), 14);
        static const QRegularExpression attribution(attributionFormat);
        for (const QcMusicTrack &track : library.tracks()) {
            QVERIFY2(attribution.match(track.attribution).hasMatch(), qPrintable(track.attribution));
            QVERIFY(track.attribution.contains(u"'%1'"_s.arg(track.title)));
            QCOMPARE(track.licence, u"CC-BY-4.0"_s);
        }
        for (const QString &slot : {u"menu"_s, u"day"_s, u"night"_s, u"cave"_s, u"dimension:qtcraft:nether"_s}) {
            QVERIFY2(!library.tracksFor(slot).isEmpty(), qPrintable(slot));
        }
        for (const QString &biome : {u"nether_wastes"_s, u"soul_sand_valley"_s, u"crimson_forest"_s, u"warped_forest"_s, u"basalt_deltas"_s}) {
            const QString loop = u"qtcraft:ambient.%1.loop"_s.arg(biome);
            QCOMPARE(library.biomeOfLoop(loop), u"qtcraft:"_s + biome);
            QCOMPARE(library.tracksFor(u"biome:qtcraft:"_s + biome).size(), 1);
        }
        QCOMPARE(library.attributions().size(), 14);
    }

    // The handbook chapter "Music" names every track of the base game exactly as its description
    // does, no other, and the licence's address (decision E-1138), in every language.
    void handbookCreditsMatchTheTracks()
    {
        const QcMusicLibrary library = QcMusicLibrary::load({QStringLiteral(QTCRAFT_SOURCE_DIR "/content/qtcraft")});
        static const QRegularExpression creditLine(u"^'.+' by .+$"_s, QRegularExpression::MultilineOption);
        for (const QString &language : {u"en"_s, u"de"_s}) {
            QFile file(QStringLiteral(QTCRAFT_SOURCE_DIR "/content/qtcraft/lang/%1.handbook.json").arg(language));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QJsonDocument::fromJson(file.readAll()).object().value(u"chapter.qtcraft.music.credits.text"_s).toString();
            QVERIFY2(text.contains(u"https://creativecommons.org/licenses/by/4.0/"_s), qPrintable(language));

            QStringList named;
            for (QRegularExpressionMatchIterator it = creditLine.globalMatch(text); it.hasNext();) {
                named.append(it.next().captured());
            }
            QCOMPARE(named, library.attributions());
        }
    }
};

QTEST_GUILESS_MAIN(TestMusicLibrary)
#include "test_music_library.moc"
