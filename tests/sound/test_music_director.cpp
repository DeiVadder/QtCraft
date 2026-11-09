// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcMusicDirector.h"
#include "common/world/QcDayCycle.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Kind = QcMusicDirector::Action::Kind;

const QString overworld = u"qtcraft:overworld"_s;
const QString nether = u"qtcraft:nether"_s;
constexpr double noon = QcDayCycle::dayLength * 0.25;

// A library of the pack "t" with one track per entry: its name and places.
std::shared_ptr<const QcMusicLibrary> library(QTemporaryDir &root, const QList<std::pair<QString, QStringList>> &tracks)
{
    const QDir pack(root.path());
    QFile manifest(pack.filePath(u"pack.json"_s));
    if (!manifest.open(QIODevice::WriteOnly)) {
        return {};
    }
    manifest.write(R"({"id": "t", "name": "t", "version": "1.0.0"})");
    manifest.close();
    pack.mkpath(u"music"_s);
    for (const auto &[name, places] : tracks) {
        QFile json(pack.filePath(u"music/%1.json"_s.arg(name)));
        QFile ogg(pack.filePath(u"music/%1.ogg"_s.arg(name)));
        if (!json.open(QIODevice::WriteOnly) || !ogg.open(QIODevice::WriteOnly)) {
            return {};
        }
        json.write(QJsonDocument(QJsonObject{{u"slots"_s, QJsonArray::fromStringList(places)}}).toJson());
        ogg.write("OggS");
    }
    return std::make_shared<const QcMusicLibrary>(QcMusicLibrary::load({root.path()}));
}

// Advances `director` by `step` seconds until it asks for something or `limit` seconds passed;
// returns the action and when.
std::pair<QcMusicDirector::Action, double> runUntilAction(QcMusicDirector &director, double limit, double step = 0.25)
{
    const auto steps = static_cast<int>(std::floor(limit / step + 1e-9));
    for (int done = 1; done <= steps; ++done) {
        const QcMusicDirector::Action action = director.advance(step);
        if (action.kind != Kind::None) {
            return {action, done * step};
        }
    }
    return {{}, limit};
}

} // namespace

// When which music plays (decision E-1137), with the time stepped by hand: places, pauses like the
// role model, no repeats, fades for a new family of places and for the other source, and the
// composed pieces where no track fits or the setting asks for them.
class TestMusicDirector : public QObject
{
    Q_OBJECT

private slots:
    // The places of a situation, the most specific first.
    void situations()
    {
        QCOMPARE(QcMusicDirector::menu().places, QStringList{u"menu"_s});
        QCOMPARE(QcMusicDirector::world(overworld, {}, noon, 0.0f).places, (QStringList{u"day"_s, u"dimension:qtcraft:overworld"_s}));
        QCOMPARE(QcMusicDirector::world(overworld, {}, QcDayCycle::midnightTime, 0.0f).places.constFirst(), u"night"_s);
        QCOMPARE(QcMusicDirector::world(overworld, {}, noon, 0.8f).places.constFirst(), u"cave"_s);
        const QcMusicDirector::Situation wastes = QcMusicDirector::world(nether, u"qtcraft:nether_wastes"_s, noon, 0.9f);
        QCOMPARE(wastes.places, (QStringList{u"biome:qtcraft:nether_wastes"_s, u"dimension:qtcraft:nether"_s}));
        QCOMPARE(wastes.family, u"dimension:qtcraft:nether"_s);
    }

    // The first track after the first delay, then the track's length, then a pause of 10 to 20
    // minutes; the next track is not the same one again.
    void tracksWithPausesBetween()
    {
        QTemporaryDir root;
        QcMusicDirector director(7);
        director.setLibrary(library(root, {{u"a"_s, {u"day"_s}}, {u"b"_s, {u"day"_s}}}));
        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        QCOMPARE(director.advance(100.0).kind, Kind::None);
        director.setEnabled(true);
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);

        const auto [first, at] = runUntilAction(director, 40.0);
        QCOMPARE(first.kind, Kind::PlayTrack);
        QVERIFY2(at >= QcMusicDirector::firstDelay.shortest && at <= QcMusicDirector::firstDelay.longest + 0.25, qPrintable(QString::number(at)));
        // Nothing more is asked for until the piece plays.
        QCOMPARE(director.advance(1000.0).kind, Kind::None);
        QVERIFY(director.started(200.0));
        QCOMPARE(director.state(), QcMusicDirector::State::Playing);
        QCOMPARE(director.advance(199.0).kind, Kind::None);
        QCOMPARE(director.advance(2.0).kind, Kind::None);
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() >= QcMusicDirector::trackPause.shortest && director.remaining() <= QcMusicDirector::trackPause.longest);

        const auto [second, pause] = runUntilAction(director, QcMusicDirector::trackPause.longest + 1.0, 1.0);
        QCOMPARE(second.kind, Kind::PlayTrack);
        QVERIFY(pause >= QcMusicDirector::trackPause.shortest);
        QVERIFY(second.track != first.track);
    }

    // The most specific place with a track wins; without any a composed piece plays in a world,
    // and the menus stay silent.
    void fallsBackToWiderPlacesAndPieces()
    {
        QTemporaryDir root;
        QcMusicDirector director(1);
        director.setLibrary(library(root, {{u"wastes"_s, {u"biome:nether_wastes"_s}}, {u"any"_s, {u"dimension:qtcraft:nether"_s}}}));
        director.setSituation(QcMusicDirector::world(nether, u"t:nether_wastes"_s, noon, 0.0f));
        director.setEnabled(true);
        QCOMPARE(runUntilAction(director, 40.0).first, (QcMusicDirector::Action{Kind::PlayTrack, u"t:wastes"_s}));
        director.failed();

        director.setSituation(QcMusicDirector::world(nether, u"t:crimson_forest"_s, noon, 0.0f));
        // The same family: the pause after the failed track stays.
        QCOMPARE(runUntilAction(director, 1300.0, 1.0).first, (QcMusicDirector::Action{Kind::PlayTrack, u"t:any"_s}));
        director.failed();

        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        QCOMPARE(runUntilAction(director, 1300.0, 1.0).first.kind, Kind::PlayGenerated);
        director.failed();
        QVERIFY(director.remaining() <= QcMusicDirector::generatedPause.longest);

        director.setSituation(QcMusicDirector::menu());
        QCOMPARE(runUntilAction(director, 3600.0, 1.0).first.kind, Kind::None);
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
    }

    // With the setting on generated music only composed pieces play, with their own pauses; the
    // switch fades a playing track out and the next piece follows soon.
    void generatedSourceFadesTheTrackOut()
    {
        QTemporaryDir root;
        QcMusicDirector director(3);
        director.setLibrary(library(root, {{u"a"_s, {u"day"_s}}}));
        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        director.setEnabled(true);
        QCOMPARE(runUntilAction(director, 40.0).first.kind, Kind::PlayTrack);
        QVERIFY(director.started(300.0));

        director.setSource(QcMusicSource::Generated);
        QCOMPARE(director.source(), QcMusicSource::Generated);
        QCOMPARE(director.advance(0.1).kind, Kind::FadeOut);
        QCOMPARE(director.state(), QcMusicDirector::State::Fading);
        QCOMPARE(director.advance(QcMusicDirector::fadeSeconds - 0.5).kind, Kind::None);
        QCOMPARE(director.advance(1.0).kind, Kind::None);
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() <= QcMusicDirector::changeDelay.longest);

        QCOMPARE(runUntilAction(director, QcMusicDirector::changeDelay.longest + 1.0).first.kind, Kind::PlayGenerated);
        QVERIFY(director.started(90.0));
        QCOMPARE(director.advance(91.0).kind, Kind::None);
        QVERIFY(director.remaining() >= QcMusicDirector::generatedPause.shortest && director.remaining() <= QcMusicDirector::generatedPause.longest);
    }

    // Day, night and caves are one family: the piece plays on. Another dimension fades it out;
    // a long pause gets short when the family changes.
    void newFamilyFadesSameFamilyPlaysOn()
    {
        QTemporaryDir root;
        QcMusicDirector director(5);
        director.setLibrary(library(root, {{u"a"_s, {u"day"_s, u"cave"_s, u"dimension:qtcraft:nether"_s}}}));
        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        director.setEnabled(true);
        QCOMPARE(runUntilAction(director, 40.0).first.kind, Kind::PlayTrack);
        QVERIFY(director.started(300.0));

        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.9f));
        QCOMPARE(director.advance(1.0).kind, Kind::None);
        QCOMPARE(director.state(), QcMusicDirector::State::Playing);

        director.setSituation(QcMusicDirector::world(nether, {}, noon, 0.0f));
        QCOMPARE(director.advance(1.0).kind, Kind::FadeOut);
        QCOMPARE(director.advance(QcMusicDirector::fadeSeconds).kind, Kind::None);
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        QVERIFY(director.remaining() <= QcMusicDirector::changeDelay.longest);

        // A long pause in the world, then the menu: its music follows soon.
        QCOMPARE(runUntilAction(director, 20.0).first.kind, Kind::PlayTrack);
        QVERIFY(director.started(10.0));
        QCOMPARE(director.advance(11.0).kind, Kind::None);
        QVERIFY(director.remaining() >= QcMusicDirector::trackPause.shortest);
        director.setSituation(QcMusicDirector::menu());
        QVERIFY(director.remaining() <= QcMusicDirector::changeDelay.longest);
    }

    // A piece that cannot play is followed by a pause; one cut off from outside by the first
    // delay; off is off.
    void failuresInterruptionsAndOff()
    {
        QTemporaryDir root;
        QcMusicDirector director(9);
        director.setLibrary(library(root, {{u"a"_s, {u"menu"_s}}}));
        director.setSituation(QcMusicDirector::menu());
        director.setEnabled(true);
        QCOMPARE(runUntilAction(director, 40.0).first.kind, Kind::PlayTrack);
        director.failed();
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() <= QcMusicDirector::menuPause.longest);

        QCOMPARE(runUntilAction(director, 40.0).first.kind, Kind::PlayTrack);
        QVERIFY(director.started(60.0));
        director.interrupted();
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() >= QcMusicDirector::firstDelay.shortest);

        director.setEnabled(false);
        QCOMPARE(director.state(), QcMusicDirector::State::Off);
        QCOMPARE(director.advance(3600.0).kind, Kind::None);
        // A piece arriving after the switch-off is not taken.
        QVERIFY(!director.started(10.0));
        QCOMPARE(director.state(), QcMusicDirector::State::Off);
    }

    // A change while a piece was asked for: the piece is refused when it arrives and the next one
    // follows soon; a piece without length counts as failed.
    void startsRefusedAfterAChangeOrWithoutLength()
    {
        QTemporaryDir root;
        QcMusicDirector director(11);
        director.setLibrary(library(root, {{u"a"_s, {u"day"_s}}}));
        director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
        director.setEnabled(true);
        QCOMPARE(runUntilAction(director, 40.0).first.kind, Kind::PlayTrack);
        director.setSource(QcMusicSource::Generated);
        QVERIFY(!director.started(300.0));
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() <= QcMusicDirector::changeDelay.longest);
        QCOMPARE(director.advance(0.1).kind, Kind::None);

        QCOMPARE(runUntilAction(director, QcMusicDirector::changeDelay.longest + 1.0).first.kind, Kind::PlayGenerated);
        QVERIFY(!director.started(0.0));
        QCOMPARE(director.state(), QcMusicDirector::State::Waiting);
        QVERIFY(director.remaining() >= QcMusicDirector::generatedPause.shortest);
    }

    // The same seed gives the same choices: the director takes no time or chance of its own.
    void sameSeedSameMusic()
    {
        QTemporaryDir root;
        const std::shared_ptr<const QcMusicLibrary> tracks = library(root, {{u"a"_s, {u"day"_s}}, {u"b"_s, {u"day"_s}}, {u"c"_s, {u"day"_s}}});
        QStringList runs[2];
        for (QStringList &run : runs) {
            QcMusicDirector director(42);
            director.setLibrary(tracks);
            director.setSituation(QcMusicDirector::world(overworld, {}, noon, 0.0f));
            director.setEnabled(true);
            for (int piece = 0; piece < 6; ++piece) {
                const auto [action, at] = runUntilAction(director, 1400.0, 1.0);
                run.append(u"%1@%2"_s.arg(action.track).arg(at));
                QVERIFY(director.started(100.0));
            }
        }
        QCOMPARE(runs[0], runs[1]);
    }
};

QTEST_GUILESS_MAIN(TestMusicDirector)
#include "test_music_director.moc"
