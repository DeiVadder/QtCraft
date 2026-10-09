#include "QcAudioEngine.h"
#include "QcAudioOutput.h"
#include "QcBaseContent.h"
#include "QcSoundKeys.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

// The real audio output. Every check works with and without an audio device: without one the
// output reports why and stays silent. With one, everything plays muted and for well under a second.
class TestAudioOutput : public QObject
{
    Q_OBJECT

private slots:
    void outputPullsTheMixerOrStaysSilent()
    {
        QcAudioOutput output;
        if (!output.start()) {
            QVERIFY(!output.isActive());
            QVERIFY(!output.error().isEmpty());
            // The mixer still accepts work; nothing ever reads it.
            output.withMixer([](QcAudioMixer &mixer) { QCOMPARE(mixer.voiceCount(), 0); });
            return;
        }

        QVERIFY(output.isActive());
        QVERIFY(output.error().isEmpty());
        const int rate = output.sampleRate();
        QVERIFY(rate >= 8000 && rate <= 384000);

        auto buffer = std::make_shared<QcSampleBuffer>();
        buffer->samples.assign(std::size_t(rate / 20), 0.5f);
        buffer->sampleRate = rate;
        QcAudioMixer::VoiceId voice = 0;
        output.withMixer([&](QcAudioMixer &mixer) {
            mixer.setMuted(true);
            voice = mixer.play(buffer, {});
        });
        QVERIFY(voice != 0);

        // A 50 ms sound gets mixed (silently); the limit leaves room for slow sanitizer builds and
        // QTRY returns as soon as it holds.
        QTRY_VERIFY_WITH_TIMEOUT(output.framesRendered() > rate / 20, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                bool playing = true;
                output.withMixer([&](QcAudioMixer &mixer) { playing = mixer.isPlaying(voice); });
                return !playing;
            }(),
            3000);
        output.stop();
        QVERIFY(!output.isActive());
    }

    void switchedOffEngineIgnoresEverything()
    {
        QcAudioEngine engine;
        engine.start(false);
        QVERIFY(!engine.isActive());
        QStringList warnings;
        engine.setContent(*QcBaseContent::load(warnings));
        QVERIFY(!engine.isBankReady());
        engine.play({QString::fromLatin1(QcSoundKeys::playerHurt), {}, 1.0f, 1.0f, QcSoundCue::Origin::Server, 0}, {}, 0.0);
        engine.setAmbience({1.0f, 1.0f, 1.0f});
        engine.setMusicEnabled(true);
        engine.output().withMixer([](QcAudioMixer &mixer) { QCOMPARE(mixer.voiceCount(), 0); });
    }

    void engineRendersTheContentAndPlaysCues()
    {
        QcAudioEngine engine;
        engine.start(true);
        if (!engine.isActive()) {
            return;
        }

        engine.setVolumes(0.0, 0.0, 0.0, 0.0, true);
        QSignalSpy ready(&engine, &QcAudioEngine::bankReady);
        QStringList warnings;
        engine.setContent(*QcBaseContent::load(warnings));
        QVERIFY(ready.wait(5000));
        QVERIFY(engine.isBankReady());

        const auto voices = [&engine] {
            int count = 0;
            engine.output().withMixer([&count](QcAudioMixer &mixer) { count = mixer.voiceCount(); });
            return count;
        };

        // In range a cue becomes a voice, out of range or unknown it does not.
        const QcVec3 ears{0.0, 65.0, 0.0};
        const QString key = QString::fromLatin1(QcSoundKeys::playerDeath);
        engine.play({key, {0.0, 65.0, 50.0}, 1.0f, 1.0f, QcSoundCue::Origin::Server, 0}, ears, 0.0);
        engine.play({u"qtcraft:no.such.sound"_s, ears, 1.0f, 1.0f, QcSoundCue::Origin::Server, 0}, ears, 0.0);
        QCOMPARE(voices(), 0);
        engine.play({key, {3.0, 65.0, 0.0}, 1.0f, 1.0f, QcSoundCue::Origin::Server, 0}, ears, 0.0);
        QCOMPARE(voices(), 1);

        // Ambience loops start with their first audible level and stop with the world.
        engine.setAmbience({0.5f, 0.0f, 0.0f});
        QCOMPARE(voices(), 2);
        engine.stopWorldSounds();
        QCOMPARE(voices(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAudioOutput)
#include "test_audio_output.moc"
