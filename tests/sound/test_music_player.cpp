// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcAudioOutput.h"
#include "audio/QcMusicPlayer.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// One output device that always opens and never pulls: the test mixes by hand.
class SilentAudio : public QcAudioPlatform
{
    Q_OBJECT

public:
    [[nodiscard]] QList<Device> outputs() const override { return {{"silent", u"Silent"_s}}; }
    [[nodiscard]] QByteArray defaultOutput() const override { return "silent"; }
    [[nodiscard]] std::optional<Format> format(const QByteArray &, int) const override { return Format{48000, QcMixerDevice::Format::Float}; }
    [[nodiscard]] Opened open(const QByteArray &, const Format &, const QcMixerDevice::Render &) override { return {QtAudio::NoError, 1}; }
    void close() override {}
    void watchOutputs(bool) override {}
};

// The mixer's music voices: whether each is a stream and fading.
struct MusicVoices
{
    int streams = 0;
    int buffers = 0;
    int fading = 0;
    // Frames left of the fade of the last fading voice.
    int fadeFrames = -1;
};

MusicVoices musicVoices(QcAudioOutput &output)
{
    MusicVoices found;
    output.withMixer([&found](QcAudioMixer &mixer) {
        for (const QcAudioMixer::Voice &voice : mixer.voices()) {
            if (voice.category == QcSoundCategory::Music) {
                found.streams += voice.stream ? 1 : 0;
                found.buffers += voice.buffer ? 1 : 0;
                found.fading += voice.fadeFrames >= 0 ? 1 : 0;
                found.fadeFrames = std::max(found.fadeFrames, voice.fadeFrames);
            }
        }
    });
    return found;
}

} // namespace

// The music player (decision E-1137) on a silent output, its time stepped by hand: it opens a track
// on a worker thread and streams it, fades it out on a change of family, renders a composed piece
// where the setting asks for one, and fails over to a pause for a broken file.
class TestMusicPlayer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_pack.isValid());
        const QDir pack(m_pack.path());
        QVERIFY(pack.mkpath(u"music"_s));
        QFile manifest(pack.filePath(u"pack.json"_s));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"id": "t", "name": "t", "version": "1.0.0"})");
        manifest.close();
        QVERIFY(QFile::copy(QStringLiteral(QTCRAFT_SOURCE_DIR "/tests/data/music/stereo_tone.ogg"), pack.filePath(u"music/tone.ogg"_s)));
        QFile tone(pack.filePath(u"music/tone.json"_s));
        QVERIFY(tone.open(QIODevice::WriteOnly));
        tone.write(R"({"slots": ["menu", "day"]})");
        tone.close();
        QFile broken(pack.filePath(u"music/broken.ogg"_s));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not an ogg file");
        broken.close();
        QFile brokenJson(pack.filePath(u"music/broken.json"_s));
        QVERIFY(brokenJson.open(QIODevice::WriteOnly));
        brokenJson.write(R"({"slots": ["night"]})");
    }

    void streamsATrackAndFadesItOut()
    {
        QcAudioOutput output(std::make_unique<SilentAudio>());
        QVERIFY(output.start());
        QcMusicPlayer player(output);
        player.director().setLibrary(std::make_shared<const QcMusicLibrary>(QcMusicLibrary::load({m_pack.path()})));
        player.director().setSituation(QcMusicDirector::menu());
        player.director().setEnabled(true);

        player.advance(QcMusicDirector::firstDelay.longest);
        QCOMPARE(player.director().state(), QcMusicDirector::State::Asking);
        QTRY_COMPARE(player.director().state(), QcMusicDirector::State::Playing);
        QCOMPARE(musicVoices(output).streams, 1);
        QVERIFY(std::abs(player.director().remaining() - 0.5) < 0.01);

        player.director().setSituation(QcMusicDirector::world(u"qtcraft:overworld"_s, {}, 150.0, 0.0f));
        player.advance(0.1);
        QCOMPARE(player.director().state(), QcMusicDirector::State::Fading);
        const MusicVoices fading = musicVoices(output);
        QCOMPARE(fading.fading, 1);
        // The whole fade lies ahead: fadeSeconds at the output's rate.
        QCOMPARE(fading.fadeFrames, static_cast<int>(QcMusicDirector::fadeSeconds * output.sampleRate()));

        player.setEnabled(false);
        QCOMPARE(musicVoices(output).streams, 0);
    }

    // The setting switched to generated music while a track opened: the track never sounds, the
    // composed piece follows after a short pause.
    void aTrackAskedForBeforeASwitchIsDropped()
    {
        QcAudioOutput output(std::make_unique<SilentAudio>());
        QVERIFY(output.start());
        QcMusicPlayer player(output);
        player.director().setLibrary(std::make_shared<const QcMusicLibrary>(QcMusicLibrary::load({m_pack.path()})));
        player.director().setSituation(QcMusicDirector::menu());
        player.director().setEnabled(true);

        player.advance(QcMusicDirector::firstDelay.longest);
        QCOMPARE(player.director().state(), QcMusicDirector::State::Asking);
        player.director().setSource(QcMusicSource::Generated);
        QTRY_COMPARE(player.director().state(), QcMusicDirector::State::Waiting);
        QCOMPARE(musicVoices(output).streams, 0);
        QCOMPARE(player.voice(), QcAudioMixer::VoiceId{0});
        QVERIFY(player.director().remaining() <= QcMusicDirector::changeDelay.longest);
    }

    void playsAComposedPieceWhenAskedTo()
    {
        QcAudioOutput output(std::make_unique<SilentAudio>());
        QVERIFY(output.start());
        QcMusicPlayer player(output);
        player.director().setLibrary(std::make_shared<const QcMusicLibrary>(QcMusicLibrary::load({m_pack.path()})));
        player.director().setSource(QcMusicSource::Generated);
        player.director().setSituation(QcMusicDirector::world(u"qtcraft:overworld"_s, {}, 150.0, 0.0f));
        player.director().setEnabled(true);

        player.advance(QcMusicDirector::firstDelay.longest);
        QTRY_COMPARE_WITH_TIMEOUT(player.director().state(), QcMusicDirector::State::Playing, 30000);
        const MusicVoices voices = musicVoices(output);
        QCOMPARE(voices.buffers, 1);
        QCOMPARE(voices.streams, 0);

        // The mixer's voices stopped from outside (leaving a world): the next piece follows after
        // the first delay.
        output.withMixer([](QcAudioMixer &mixer) { mixer.stopAll(); });
        player.interrupted();
        QCOMPARE(player.director().state(), QcMusicDirector::State::Waiting);
    }

    void aBrokenFileIsSkipped()
    {
        QcAudioOutput output(std::make_unique<SilentAudio>());
        QVERIFY(output.start());
        QcMusicPlayer player(output);
        player.director().setLibrary(std::make_shared<const QcMusicLibrary>(QcMusicLibrary::load({m_pack.path()})));
        player.director().setSituation(QcMusicDirector::world(u"qtcraft:overworld"_s, {}, 450.0, 0.0f));
        player.director().setEnabled(true);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Music track not played:.*broken\\.ogg"_s));
        player.advance(QcMusicDirector::firstDelay.longest);
        QTRY_COMPARE(player.director().state(), QcMusicDirector::State::Waiting);
        QCOMPARE(musicVoices(output).streams, 0);
        QVERIFY(player.director().remaining() >= QcMusicDirector::trackPause.shortest);
    }

private:
    QTemporaryDir m_pack;
};

QTEST_MAIN(TestMusicPlayer)
#include "test_music_player.moc"
